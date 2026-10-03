// ZAN Tech Robotic Arm Controller - Mobile (Flutter)
//
// A clean, modern Bluetooth remote for the 4-Servo / 4-DOF / 6-DOF robotic
// arms in this repo. Talks to any of the HC-05 or ESP32 Bluetooth firmware
// variants (firmware/*-hc05 and firmware/*-esp32) using the exact same
// serial command protocol as the desktop PC app (pc-app/arm_controller_gui.py)
// and the Arduino Serial Monitor - e.g. "B90\n" moves the Base servo to 90°.
//
// Pick an arm type, pick a paired Bluetooth device, connect, then drag the
// sliders. Bluetooth Classic (RFCOMM/SPP) is handled by flutter_classic_bluetooth.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_classic_bluetooth/flutter_classic_bluetooth.dart';

void main() {
  runApp(const ZanTechArmApp());
}

// --------------------------------------------------------------- Brand
class Brand {
  static const red = Color(0xFFFD110F);
  static const blue = Color(0xFF2A2BB7);
  static const blueBright = Color(0xFF4850E6);

  static const bgWindow = Color(0xFF0A0E1C);
  static const bgPanel = Color(0xFF121833);
  static const bgPanelAlt = Color(0xFF171F45);
  static const bgField = Color(0xFF0E1430);
  static const textPrimary = Color(0xFFF3F5FC);
  static const textMuted = Color(0xFF8B93B8);
  static const ok = Color(0xFF2ECC71);
  static const error = Color(0xFFFF4D4D);
  static const warn = Color(0xFFFFB020);
}

// --------------------------------------------------------- Arm definitions
// Letter codes must match the single-letter codes each firmware's .ino
// expects (see firmware/*/*.ino). Same three arms as the PC app.
class ServoDef {
  final String name;
  final String letter;
  const ServoDef(this.name, this.letter);
}

class ArmProfile {
  final String key;
  final String label;
  final String firmwareHint;
  final List<ServoDef> servos;
  const ArmProfile(this.key, this.label, this.firmwareHint, this.servos);
}

const List<ArmProfile> armProfiles = [
  ArmProfile('4servo', '4-Servo Arm', '4 servos - firmware/4servo-arm-*', [
    ServoDef('Base', 'B'),
    ServoDef('Shoulder', 'S'),
    ServoDef('Elbow', 'E'),
    ServoDef('Gripper', 'G'),
  ]),
  ArmProfile('4dof', '4-DOF Arm', '5 servos - firmware/4dof-arm-*', [
    ServoDef('Base', 'B'),
    ServoDef('Shoulder', 'S'),
    ServoDef('Elbow', 'E'),
    ServoDef('Wrist', 'W'),
    ServoDef('Gripper', 'G'),
  ]),
  ArmProfile('6dof', '6-DOF Arm', '6 servos - firmware/6dof-arm-*', [
    ServoDef('Base', 'B'),
    ServoDef('Shoulder', 'S'),
    ServoDef('Elbow', 'E'),
    ServoDef('Wrist Pitch', 'P'),
    ServoDef('Wrist Roll', 'R'),
    ServoDef('Gripper', 'G'),
  ]),
];

const int homeAngle = 90;
const int sliderMin = 0;
const int sliderMax = 180;

class ZanTechArmApp extends StatelessWidget {
  const ZanTechArmApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ZAN Tech Arm Controller',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        scaffoldBackgroundColor: Brand.bgWindow,
        colorScheme: ColorScheme.fromSeed(
          seedColor: Brand.blue,
          brightness: Brightness.dark,
          primary: Brand.blueBright,
          secondary: Brand.red,
          surface: Brand.bgPanel,
        ),
        appBarTheme: const AppBarTheme(
          backgroundColor: Brand.bgWindow,
          elevation: 0,
          foregroundColor: Brand.textPrimary,
        ),
        sliderTheme: SliderThemeData(
          activeTrackColor: Brand.blueBright,
          inactiveTrackColor: Brand.bgField,
          thumbColor: Brand.blueBright,
          overlayColor: Brand.blueBright.withValues(alpha: 0.2),
          valueIndicatorColor: Brand.blueBright,
        ),
        textTheme: const TextTheme(
          bodyMedium: TextStyle(color: Brand.textPrimary),
        ),
      ),
      home: const ArmControllerPage(),
    );
  }
}

class ArmControllerPage extends StatefulWidget {
  const ArmControllerPage({super.key});
  @override
  State<ArmControllerPage> createState() => _ArmControllerPageState();
}

class _ArmControllerPageState extends State<ArmControllerPage> {
  final _bt = FlutterClassicBluetooth();

  ArmProfile _profile = armProfiles[0];
  final Map<String, int> _angles = {};

  BtcConnection? _connection;
  StreamSubscription? _inputSub;
  StreamSubscription? _stateSub;
  bool _connecting = false;
  BtcDevice? _connectedDevice;

  final List<_LogLine> _log = [];
  bool _logVisible = false;
  final _commandController = TextEditingController();
  final _scrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    _resetAngles();
  }

  void _resetAngles() {
    _angles.clear();
    for (final s in _profile.servos) {
      _angles[s.letter] = homeAngle;
    }
  }

  @override
  void dispose() {
    _inputSub?.cancel();
    _stateSub?.cancel();
    _connection?.dispose();
    _commandController.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  bool get _isConnected => _connection?.isConnected ?? false;

  void _appendLog(String text, {_LogKind kind = _LogKind.system}) {
    setState(() {
      _log.add(_LogLine(text, kind));
      if (_log.length > 300) _log.removeAt(0);
    });
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scrollController.hasClients) {
        _scrollController.animateTo(
          _scrollController.position.maxScrollExtent,
          duration: const Duration(milliseconds: 150),
          curve: Curves.easeOut,
        );
      }
    });
  }

  // ---------------------------------------------------------- Device picker
  Future<void> _openDevicePicker() async {
    final caps = await _bt.getPlatformCapabilities();
    final status = await _bt.checkPermissions();
    if (status == BtcPermissionStatus.denied) {
      final req = await _bt.requestPermissions();
      if (req != BtcPermissionStatus.granted &&
          req != BtcPermissionStatus.notRequired) {
        _appendLog('Bluetooth permission denied.');
        return;
      }
    } else if (status == BtcPermissionStatus.permanentlyDenied) {
      _appendLog('Bluetooth permission permanently denied - open app settings.');
      await _bt.openAppSettings();
      return;
    }

    if (!mounted) return;
    await showModalBottomSheet(
      context: context,
      backgroundColor: Brand.bgPanel,
      isScrollControlled: true,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
      ),
      builder: (ctx) => _DevicePickerSheet(
        bt: _bt,
        canDiscover: caps.canDiscoverDevices,
        onDeviceSelected: (device) {
          Navigator.of(ctx).pop();
          _connectTo(device);
        },
      ),
    );
  }

  Future<void> _connectTo(BtcDevice device) async {
    setState(() => _connecting = true);
    _appendLog('Connecting to ${device.displayName} (${device.address})...');
    try {
      final conn = await _bt.connect(
        address: device.address,
        timeout: const Duration(seconds: 10),
      );
      _connection = conn;
      _connectedDevice = device;
      _inputSub = conn.input.lines().listen((line) {
        if (line.trim().isEmpty) return;
        _appendLog(line, kind: _LogKind.recv);
      });
      _stateSub = conn.stateStream.listen((state) {
        if (state == BtcConnectionState.disconnected) {
          _appendLog('Disconnected.');
          setState(() {});
        }
      });
      setState(() => _connecting = false);
      _appendLog('Connected.');
      await _sendRaw('POS');
    } catch (e) {
      setState(() => _connecting = false);
      _appendLog('Connect failed: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Could not connect: $e')),
        );
      }
    }
  }

  Future<void> _disconnect() async {
    await _inputSub?.cancel();
    await _stateSub?.cancel();
    await _connection?.close();
    _connection?.dispose();
    _connection = null;
    _connectedDevice = null;
    _appendLog('Disconnected.');
    setState(() {});
  }

  // -------------------------------------------------------------- Commands
  Future<void> _sendRaw(String command) async {
    final conn = _connection;
    if (conn == null || !conn.isConnected) {
      _appendLog('Not connected - tap "Connect" first.');
      return;
    }
    try {
      await conn.output.writeLine(command);
      _appendLog('> $command', kind: _LogKind.sent);
    } catch (e) {
      _appendLog('Send failed: $e');
    }
  }

  void _onSliderChanged(String letter, double value) {
    final angle = value.round();
    setState(() => _angles[letter] = angle);
  }

  void _onSliderChangeEnd(String letter, double value) {
    final angle = value.round();
    _sendRaw('$letter$angle');
  }

  Future<void> _sendHomeAll() async {
    setState(_resetAngles);
    await _sendRaw('HOMEALL');
  }

  Future<void> _sendRefresh() async {
    await _sendRaw('POS');
  }

  void _onProfileChanged(ArmProfile? profile) {
    if (profile == null) return;
    setState(() {
      _profile = profile;
      _resetAngles();
    });
  }

  void _syncSlidersFromCommand(String raw) {
    final upper = raw.toUpperCase();
    if (upper == 'HOMEALL') {
      setState(_resetAngles);
      return;
    }
    final letters = _profile.servos.map((s) => s.letter).toSet();
    int i = 0;
    while (i < upper.length) {
      final letter = upper[i];
      if (letters.contains(letter)) {
        int j = i + 1;
        while (j < upper.length &&
            upper.codeUnitAt(j) >= 48 &&
            upper.codeUnitAt(j) <= 57) {
          j++;
        }
        if (j > i + 1) {
          final angle = int.tryParse(upper.substring(i + 1, j));
          if (angle != null && angle >= sliderMin && angle <= sliderMax) {
            setState(() => _angles[letter] = angle);
          }
          i = j;
          continue;
        }
      }
      i++;
    }
  }

  Future<void> _sendManualCommand() async {
    final command = _commandController.text.trim();
    if (command.isEmpty) return;
    await _sendRaw(command);
    _syncSlidersFromCommand(command);
    _commandController.clear();
  }

  // ------------------------------------------------------------------ UI
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        titleSpacing: 16,
        title: Row(
          children: [
            Image.asset('assets/images/app_icon.png',
                height: 28,
                errorBuilder: (context, error, stackTrace) =>
                    const SizedBox()),
            const SizedBox(width: 10),
            RichText(
              text: const TextSpan(children: [
                TextSpan(
                  text: 'ZAN',
                  style: TextStyle(
                      color: Brand.red,
                      fontWeight: FontWeight.w800,
                      fontSize: 18),
                ),
                TextSpan(
                  text: 'TECH',
                  style: TextStyle(
                      color: Brand.textPrimary,
                      fontWeight: FontWeight.w800,
                      fontSize: 18),
                ),
              ]),
            ),
          ],
        ),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 16),
            child: Row(
              children: [
                Container(
                  width: 10,
                  height: 10,
                  decoration: BoxDecoration(
                    color: _isConnected ? Brand.ok : Brand.error,
                    shape: BoxShape.circle,
                  ),
                ),
                const SizedBox(width: 6),
                Text(
                  _connecting
                      ? 'Connecting...'
                      : (_isConnected
                          ? (_connectedDevice?.displayName ?? 'Connected')
                          : 'Disconnected'),
                  style: const TextStyle(color: Brand.textMuted, fontSize: 12),
                ),
              ],
            ),
          ),
        ],
      ),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
          children: [
            _buildSetupCard(),
            const SizedBox(height: 16),
            _buildServoCard(),
            const SizedBox(height: 16),
            _buildQuickActions(),
            const SizedBox(height: 12),
            _buildConsole(),
          ],
        ),
      ),
    );
  }

  Widget _buildSetupCard() {
    return Container(
      decoration: BoxDecoration(
        color: Brand.bgPanel,
        borderRadius: BorderRadius.circular(16),
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Text('ARM TYPE',
              style: TextStyle(
                  color: Brand.textMuted,
                  fontSize: 11,
                  fontWeight: FontWeight.bold,
                  letterSpacing: 1)),
          const SizedBox(height: 8),
          DropdownButtonFormField<ArmProfile>(
            initialValue: _profile,
            dropdownColor: Brand.bgField,
            decoration: InputDecoration(
              filled: true,
              fillColor: Brand.bgField,
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(10),
                borderSide: BorderSide.none,
              ),
              contentPadding:
                  const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
            ),
            items: armProfiles
                .map((p) => DropdownMenuItem(
                      value: p,
                      child: Text('${p.label}  ·  ${p.servos.length} servos'),
                    ))
                .toList(),
            onChanged: _onProfileChanged,
          ),
          const SizedBox(height: 16),
          ElevatedButton.icon(
            onPressed: _isConnected
                ? _disconnect
                : (_connecting ? null : _openDevicePicker),
            style: ElevatedButton.styleFrom(
              backgroundColor: _isConnected ? Brand.bgPanelAlt : Brand.blue,
              foregroundColor: Brand.textPrimary,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape:
                  RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
            ),
            icon: Icon(_isConnected
                ? Icons.bluetooth_disabled
                : Icons.bluetooth_searching),
            label: Text(_connecting
                ? 'Connecting...'
                : (_isConnected ? 'Disconnect' : 'Connect')),
          ),
        ],
      ),
    );
  }

  Widget _buildServoCard() {
    return Container(
      decoration: BoxDecoration(
        color: Brand.bgPanel,
        borderRadius: BorderRadius.circular(16),
      ),
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Text('SERVOS',
              style: TextStyle(
                  color: Brand.textMuted,
                  fontSize: 11,
                  fontWeight: FontWeight.bold,
                  letterSpacing: 1)),
          const SizedBox(height: 4),
          for (final servo in _profile.servos) _buildServoRow(servo),
        ],
      ),
    );
  }

  Widget _buildServoRow(ServoDef servo) {
    final angle = _angles[servo.letter] ?? homeAngle;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          SizedBox(
            width: 92,
            child: Text(servo.name,
                style:
                    const TextStyle(color: Brand.textPrimary, fontSize: 14)),
          ),
          Expanded(
            child: Slider(
              value: angle.toDouble(),
              min: sliderMin.toDouble(),
              max: sliderMax.toDouble(),
              divisions: sliderMax - sliderMin,
              onChanged: (v) => _onSliderChanged(servo.letter, v),
              onChangeEnd: (v) => _onSliderChangeEnd(servo.letter, v),
            ),
          ),
          Container(
            width: 42,
            height: 28,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: Brand.blue,
              borderRadius: BorderRadius.circular(8),
            ),
            child: Text('$angle',
                style: const TextStyle(
                    color: Colors.white,
                    fontSize: 12,
                    fontWeight: FontWeight.bold)),
          ),
        ],
      ),
    );
  }

  Widget _buildQuickActions() {
    return Row(
      children: [
        Expanded(
          child: ElevatedButton.icon(
            onPressed: _sendHomeAll,
            style: ElevatedButton.styleFrom(
              backgroundColor: Brand.red,
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape:
                  RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
            ),
            icon: const Icon(Icons.restart_alt),
            label: const Text('Home All'),
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: ElevatedButton.icon(
            onPressed: _sendRefresh,
            style: ElevatedButton.styleFrom(
              backgroundColor: Brand.bgPanelAlt,
              foregroundColor: Brand.textPrimary,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape:
                  RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
            ),
            icon: const Icon(Icons.refresh),
            label: const Text('Refresh'),
          ),
        ),
      ],
    );
  }

  Widget _buildConsole() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        TextButton.icon(
          onPressed: () => setState(() => _logVisible = !_logVisible),
          icon: Icon(
              _logVisible ? Icons.keyboard_arrow_down : Icons.chevron_right,
              color: Brand.textMuted,
              size: 18),
          label: Text(
            _logVisible ? 'Hide serial console' : 'Show serial console',
            style: const TextStyle(color: Brand.textMuted, fontSize: 13),
          ),
        ),
        if (_logVisible) ...[
          Container(
            height: 200,
            decoration: BoxDecoration(
              color: Brand.bgField,
              borderRadius: BorderRadius.circular(10),
            ),
            padding: const EdgeInsets.all(10),
            child: ListView.builder(
              controller: _scrollController,
              itemCount: _log.length,
              itemBuilder: (context, index) {
                final line = _log[index];
                return Text(
                  line.text,
                  style: TextStyle(
                    fontFamily: 'monospace',
                    fontSize: 12,
                    color: switch (line.kind) {
                      _LogKind.sent => Brand.blueBright,
                      _LogKind.recv => Brand.ok,
                      _LogKind.system => Brand.textMuted,
                    },
                  ),
                );
              },
            ),
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: _commandController,
                  style: const TextStyle(
                      color: Brand.textPrimary, fontFamily: 'monospace'),
                  decoration: InputDecoration(
                    hintText: 'MENU, POS, HOMEALL, T1, B45 E120 ...',
                    hintStyle: const TextStyle(color: Brand.textMuted),
                    filled: true,
                    fillColor: Brand.bgField,
                    contentPadding: const EdgeInsets.symmetric(
                        horizontal: 12, vertical: 10),
                    border: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(10),
                      borderSide: BorderSide.none,
                    ),
                  ),
                  onSubmitted: (_) => _sendManualCommand(),
                ),
              ),
              const SizedBox(width: 8),
              IconButton.filled(
                onPressed: _sendManualCommand,
                style: IconButton.styleFrom(backgroundColor: Brand.blue),
                icon: const Icon(Icons.send, size: 18),
              ),
            ],
          ),
          const SizedBox(height: 4),
          const Text(
            'Type any firmware command here, same as the Arduino Serial Monitor.',
            style: TextStyle(color: Brand.textMuted, fontSize: 11),
          ),
        ],
      ],
    );
  }
}

enum _LogKind { sent, recv, system }

class _LogLine {
  final String text;
  final _LogKind kind;
  _LogLine(this.text, this.kind);
}

// ---------------------------------------------------------- Device picker UI
class _DevicePickerSheet extends StatefulWidget {
  final FlutterClassicBluetooth bt;
  final bool canDiscover;
  final void Function(BtcDevice) onDeviceSelected;

  const _DevicePickerSheet({
    required this.bt,
    required this.canDiscover,
    required this.onDeviceSelected,
  });

  @override
  State<_DevicePickerSheet> createState() => _DevicePickerSheetState();
}

class _DevicePickerSheetState extends State<_DevicePickerSheet> {
  List<BtcDevice> _paired = [];
  final List<BtcDevice> _discovered = [];
  bool _scanning = false;
  StreamSubscription? _discoverySub;

  @override
  void initState() {
    super.initState();
    _loadPaired();
  }

  @override
  void dispose() {
    _discoverySub?.cancel();
    if (_scanning) widget.bt.stopDiscovery();
    super.dispose();
  }

  Future<void> _loadPaired() async {
    final devices = await widget.bt.getPairedDevices();
    if (mounted) setState(() => _paired = devices);
  }

  Future<void> _toggleScan() async {
    if (_scanning) {
      await widget.bt.stopDiscovery();
      await _discoverySub?.cancel();
      if (mounted) setState(() => _scanning = false);
      return;
    }
    setState(() {
      _scanning = true;
      _discovered.clear();
    });
    _discoverySub = widget.bt.discoveryResults.listen((device) {
      if (!_discovered.any((d) => d.address == device.address)) {
        if (mounted) setState(() => _discovered.add(device));
      }
    });
    try {
      await widget.bt.startDiscovery();
    } catch (e) {
      if (mounted) setState(() => _scanning = false);
    }
    Future.delayed(const Duration(seconds: 12), () {
      if (mounted && _scanning) _toggleScan();
    });
  }

  @override
  Widget build(BuildContext context) {
    return DraggableScrollableSheet(
      initialChildSize: 0.65,
      minChildSize: 0.4,
      maxChildSize: 0.9,
      expand: false,
      builder: (context, scrollController) {
        return Padding(
          padding: const EdgeInsets.fromLTRB(16, 16, 16, 24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const Text('Select a device',
                      style: TextStyle(
                          color: Brand.textPrimary,
                          fontSize: 18,
                          fontWeight: FontWeight.bold)),
                  if (widget.canDiscover)
                    TextButton.icon(
                      onPressed: _toggleScan,
                      icon: _scanning
                          ? const SizedBox(
                              width: 14,
                              height: 14,
                              child: CircularProgressIndicator(strokeWidth: 2))
                          : const Icon(Icons.search, size: 18),
                      label: Text(_scanning ? 'Scanning...' : 'Scan'),
                    ),
                ],
              ),
              const SizedBox(height: 8),
              Expanded(
                child: ListView(
                  controller: scrollController,
                  children: [
                    if (_paired.isNotEmpty) ...[
                      const _SectionLabel('PAIRED'),
                      for (final d in _paired)
                        _DeviceTile(
                            device: d,
                            onTap: () => widget.onDeviceSelected(d)),
                    ],
                    if (_discovered.isNotEmpty) ...[
                      const _SectionLabel('DISCOVERED'),
                      for (final d in _discovered)
                        _DeviceTile(
                            device: d,
                            onTap: () => widget.onDeviceSelected(d)),
                    ],
                    if (_paired.isEmpty && _discovered.isEmpty)
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 24),
                        child: Text(
                          widget.canDiscover
                              ? 'No paired devices yet. Pair your HC-05 / ESP32 '
                                  'in Bluetooth settings first, or tap Scan.'
                              : 'No paired devices found. Pair your HC-05 / ESP32 '
                                  'in Bluetooth settings first.',
                          style: const TextStyle(color: Brand.textMuted),
                        ),
                      ),
                  ],
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _SectionLabel extends StatelessWidget {
  final String text;
  const _SectionLabel(this.text);
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Text(text,
          style: const TextStyle(
              color: Brand.textMuted,
              fontSize: 11,
              fontWeight: FontWeight.bold,
              letterSpacing: 1)),
    );
  }
}

class _DeviceTile extends StatelessWidget {
  final BtcDevice device;
  final VoidCallback onTap;
  const _DeviceTile({required this.device, required this.onTap});

  @override
  Widget build(BuildContext context) {
    return Card(
      color: Brand.bgField,
      margin: const EdgeInsets.only(bottom: 8),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
      child: ListTile(
        leading: const Icon(Icons.memory, color: Brand.blueBright),
        title: Text(device.displayName,
            style: const TextStyle(color: Brand.textPrimary)),
        subtitle: Text(device.address,
            style: const TextStyle(color: Brand.textMuted, fontSize: 12)),
        onTap: onTap,
      ),
    );
  }
}
