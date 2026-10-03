import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:zantech_arm_controller/main.dart';

void main() {
  testWidgets('App builds and shows the arm controller UI',
      (WidgetTester tester) async {
    await tester.pumpWidget(const ZanTechArmApp());
    await tester.pump();

    expect(find.byType(AppBar), findsOneWidget);
    expect(find.byType(DropdownButtonFormField<ArmProfile>), findsOneWidget);
    expect(find.byIcon(Icons.bluetooth_searching), findsOneWidget);
    expect(find.byIcon(Icons.restart_alt), findsOneWidget);
    expect(find.byIcon(Icons.refresh), findsOneWidget);
    // One slider per servo of the default (4-Servo) profile.
    expect(find.byType(Slider), findsNWidgets(armProfiles.first.servos.length));
  });
}
