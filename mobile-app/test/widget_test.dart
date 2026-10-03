import 'package:flutter_test/flutter_test.dart';

import 'package:zantech_arm_controller/main.dart';

void main() {
  testWidgets('App shows the ZAN Tech title and arm type selector',
      (WidgetTester tester) async {
    await tester.pumpWidget(const ZanTechArmApp());

    expect(find.text('ZAN'), findsOneWidget);
    expect(find.text('TECH'), findsOneWidget);
    expect(find.text('Connect'), findsOneWidget);
    expect(find.text('Home All'), findsOneWidget);
  });
}
