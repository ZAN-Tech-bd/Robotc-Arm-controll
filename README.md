Robotic Arm Controller

A simple 5-servo robotic arm controlled by an Arduino Nano and a Python desktop app. The arm has a base, shoulder, elbow, wrist, and gripper, and the PC interface lets you move each joint with sliders instead of manually typing commands.

## Project structure

- `4-Dof-Robotc-Arm-controll.ino` — Arduino firmware that drives the servos and listens for serial commands.
- `pc-app/arm_controller_gui.py` — Tkinter desktop app for controlling the arm from a PC.
- `pc-app/requirements.txt` — Python package requirements for the GUI app.
- `pc-app/Run Arm Controller.bat` — double-click this to open the app. No typing commands needed.

## Features

- 5 servo control: Base, Shoulder, Elbow, Wrist, Gripper
- Serial communication between Arduino and PC
- GUI slider-based control
- Home all command
- Position reporting from the arm
- Single-servo testing mode and full-arm commands

## Hardware setup

Use 5 standard 180° hobby servos and wire them as follows:

| Servo | Arduino pin |
|---|---|
| Base | D3 |
| Shoulder | D5 |
| Elbow | D6 |
| Wrist | D9 |
| Gripper | D10 |

Important:
- Connect all servo grounds to the Arduino ground if using a shared power setup.
- For better reliability, power the servos from a stable 5V external supply, especially if multiple servos move at once.

## Arduino firmware

1. Open `4-Dof-Robotc-Arm-controll.ino` in the Arduino IDE.
2. Select `Board: Arduino Nano`.
3. Choose the correct COM port.
4. Click Upload.

The firmware starts each servo at 90°, lets it settle for half a second, then stops
sending it a signal until the next command arrives — so the arm holds still and never
drifts or twitches on its own while idle. It listens on serial port 9600 baud.

When you send a new angle, the servo doesn't jump straight there — it steps smoothly in
5° increments (0, 5, 10, 15 ... 90) with a short pause between steps, so the motion looks
natural instead of snapping. You can tune this in the `.ino` file:

```cpp
const uint8_t       SERVO_STEP_DEGREES  = 5;  // degrees per step
const unsigned long SERVO_STEP_DELAY_MS = 25; // pause between steps (ms)
```

Smaller step / longer delay = slower and smoother. Larger step / shorter delay = faster
and snappier. Note: when a command moves several servos at once (e.g. `B90 S90 E90`),
each one sweeps to its target one after another, not all at the same time.

## PC app setup

### Easiest way — just double-click it

1. Install **Python 3** from [python.org](https://www.python.org/downloads/) if you don't
   have it (tick **"Add Python to PATH"** during install — it's a checkbox on the first screen).
2. Open the `pc-app` folder in File Explorer.
3. Double-click **`Run Arm Controller.bat`**.

That's it — no terminal, no typing `python` commands. The first time you run it, it will
quickly install the one required package (`pyserial`) by itself, then open the app window.
If Python isn't installed yet, it tells you so instead of just doing nothing.

> If double-clicking it does nothing and no window appears, a file named `crash_log.txt`
> will show up next to it with the error details — that usually means Python isn't
> installed correctly. See the error message the `.bat` prints, or the log file.

### Manual way (optional)

From the `pc-app` folder:

```bash
pip install -r requirements.txt
python arm_controller_gui.py
```

### Using the app

1. Select the Arduino COM port (it's labeled with its description, e.g. "COM3 - USB-SERIAL CH340" — pick that, not a plain "COM1").
2. Click Connect.
3. Move the sliders to control the arm.
4. Use Home All to reset all joints to 90°.
5. Use Refresh Positions to read the current servo angles.

## Serial commands

The firmware supports commands like:

```text
MENU
HOMEALL
POS
T1
T2
T3
T4
T5
B90 S90 E90 W90 G90
B45 E120
```

These commands are also the same ones the GUI uses behind the scenes.

## Troubleshooting

- If the app cannot connect, close the Arduino Serial Monitor first.
- If no port appears, click Refresh and confirm the USB cable is a data cable.
- If the servos twitch or reset, use a stronger 5V supply and share the ground.
- If the arm moves incorrectly, check that each signal wire is connected to the correct pin.
- If a servo seems to "move by itself" with no command sent, that was PWM jitter from an
  idle servo still being held under signal — the firmware now detaches each servo once it
  settles at its target, so this shouldn't happen anymore. If it still does, it's a power
  supply issue (see above), not a firmware issue.
- If your arm instead needs constant holding torque while idle (e.g. the shoulder sags
  under its own weight with no signal), set `DETACH_WHEN_IDLE` to `false` near the top of
  the `.ino` file and re-upload.

## License

This project is intended for educational and personal robotics use.
