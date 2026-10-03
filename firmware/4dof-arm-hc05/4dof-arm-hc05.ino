/*
  4-DOF Robotic Arm Controller - HC-05 Bluetooth version (Arduino Nano)
  5 Servos: Base, Shoulder, Elbow, Wrist, Gripper
  All servos are 180-degree servos, assembled/centered at 90 degrees.

  This is the Bluetooth sibling of firmware/4dof-arm/4dof-arm.ino - same
  serial protocol, same menu, same behavior. The ONLY difference is where
  commands come from: this version also listens on an HC-05 Bluetooth module
  (wired to two extra pins via SoftwareSerial), so you can control the arm
  wirelessly from a phone (e.g. "Serial Bluetooth Terminal" app) instead of a
  USB cable. USB Serial still works at the same time, for debugging with the
  Arduino IDE.

  ---------------- HC-05 WIRING ----------------
  HC-05 VCC  -> 5V (or 3.3V if your HC-05 board requires it - check its label)
  HC-05 GND  -> GND
  HC-05 TXD  -> Nano D2   (RX side of SoftwareSerial)
  HC-05 RXD  -> Nano D4   (TX side of SoftwareSerial)
          NOTE: HC-05 RXD is 3.3V logic. If your module has no onboard level
          shifter, put a simple voltage divider (e.g. 1k + 2k resistors)
          between Nano D4 and HC-05 RXD to avoid feeding it 5V.
  Default HC-05 data-mode baud rate is 9600, matching BT_BAUD below - if your
  module was reconfigured to a different baud, change BT_BAUD to match.

  Pair the HC-05 with your phone/PC (default PIN is usually 1234 or 0000).
  Once paired, open any serial Bluetooth terminal app and send the exact same
  commands described below - the HC-05 just becomes another serial port.

  ---------------- SERIAL / BLUETOOTH COMMANDS ----------------
  MENU                 -> show the menu again

  --- Single servo test mode ---
  T1                   -> select servo 1 (Base)      for testing
  T2                   -> select servo 2 (Shoulder)  for testing
  T3                   -> select servo 3 (Elbow)     for testing
  T4                   -> select servo 4 (Wrist)     for testing
  T5                   -> select servo 5 (Gripper)   for testing
  <number 0-180>       -> once a servo is selected (T1..T5), just type an
                          angle and press Enter to move ONLY that servo
  HOME                 -> send the selected servo back to 90 degrees
  EXIT                 -> leave single test mode

  --- Full control mode (all 5 servos in one line) ---
  B90 S90 E90 W90 G90  -> set Base, Shoulder, Elbow, Wrist, Gripper angles
                          in one command (order doesn't matter). You don't
                          need to include all five letters; only the ones you
                          type will move, e.g: B45 E120
  HOMEALL              -> send all 5 servos to 90 degrees (center/assembly pose)
  POS                  -> print the current angle of all servos
  --------------------------------------------------
*/

#include <Servo.h>
#include <SoftwareSerial.h>

// ---------- Bluetooth (HC-05) wiring ----------
const uint8_t BT_RX_PIN = 2; // to HC-05 TXD
const uint8_t BT_TX_PIN = 4; // to HC-05 RXD (through a voltage divider - see above)
const long    BT_BAUD   = 9600;
SoftwareSerial bt(BT_RX_PIN, BT_TX_PIN);

// ---------- Pin configuration ----------
const uint8_t PIN_BASE     = 3;
const uint8_t PIN_SHOULDER = 5;
const uint8_t PIN_ELBOW    = 6;
const uint8_t PIN_WRIST    = 9;
const uint8_t PIN_GRIPPER  = 10;

const uint8_t NUM_SERVOS = 5;

Servo servos[NUM_SERVOS];
const uint8_t servoPins[NUM_SERVOS] = { PIN_BASE, PIN_SHOULDER, PIN_ELBOW, PIN_WRIST, PIN_GRIPPER };
const char*   servoNames[NUM_SERVOS] = { "Base", "Shoulder", "Elbow", "Wrist", "Gripper" };
int           servoAngle[NUM_SERVOS];
bool          servoAttached[NUM_SERVOS];
unsigned long servoMoveDeadline[NUM_SERVOS];

const int HOME_ANGLE = 90;
const int ANGLE_MIN  = 0;
const int ANGLE_MAX  = 180;

const bool          DETACH_WHEN_IDLE   = true;
const unsigned long SETTLE_TIME_MS     = 500;

const uint8_t        SERVO_STEP_DEGREES = 5;
const unsigned long  SERVO_STEP_DELAY_MS = 25;

// ---------- Mode state ----------
bool    singleTestMode   = false;
int8_t  selectedServo    = -1;

String inputLine = "";

void setup() {
  Serial.begin(9600);
  bt.begin(BT_BAUD);

  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(servoPins[i]);
    servoAttached[i] = true;
    servoAngle[i] = HOME_ANGLE;
    servos[i].write(HOME_ANGLE);
    servoMoveDeadline[i] = millis() + SETTLE_TIME_MS;
  }

  delay(50);
  while (Serial.available() > 0) Serial.read();
  while (bt.available() > 0) bt.read();

  printMenu();
}

void loop() {
  readStreamInto(Serial);
  readStreamInto(bt);
  releaseSettledServos();
}

template <typename StreamT>
void readStreamInto(StreamT &stream) {
  while (stream.available() > 0) {
    char c = stream.read();
    if (c == '\n' || c == '\r') {
      if (inputLine.length() > 0) {
        handleCommand(inputLine);
        inputLine = "";
      }
    } else {
      inputLine += c;
    }
  }
}

void releaseSettledServos() {
  if (!DETACH_WHEN_IDLE) return;

  unsigned long now = millis();
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    if (servoAttached[i] && (long)(now - servoMoveDeadline[i]) >= 0) {
      servos[i].detach();
      servoAttached[i] = false;
    }
  }
}

void outPrint(const String &s) {
  Serial.print(s);
  bt.print(s);
}
void outPrintln(const String &s) {
  Serial.println(s);
  bt.println(s);
}

// ---------------- Command handling ----------------

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  String upper = line;
  upper.toUpperCase();

  if (upper == "MENU") {
    printMenu();
    return;
  }

  if (upper == "POS") {
    printPositions();
    return;
  }

  if (upper == "HOMEALL") {
    for (uint8_t i = 0; i < NUM_SERVOS; i++) moveServo(i, HOME_ANGLE);
    outPrintln(F("All servos moved to HOME (90 degrees)."));
    return;
  }

  if (upper.length() == 2 && upper.charAt(0) == 'T' && isDigit(upper.charAt(1))) {
    int idx = upper.charAt(1) - '1';
    if (idx >= 0 && idx < NUM_SERVOS) {
      singleTestMode = true;
      selectedServo = idx;
      outPrint(F("-- TEST MODE: "));
      outPrint(servoNames[idx]);
      outPrintln(F(" selected. Type an angle (0-180) and press Enter."));
      outPrintln(F("   Type HOME to center it, EXIT to leave test mode."));
    } else {
      outPrintln(F("Invalid servo number. Use T1 to T5."));
    }
    return;
  }

  if (singleTestMode) {
    if (upper == "EXIT") {
      outPrint(F("Exiting test mode for "));
      outPrintln(servoNames[selectedServo]);
      singleTestMode = false;
      selectedServo = -1;
      return;
    }
    if (upper == "HOME") {
      moveServo(selectedServo, HOME_ANGLE);
      outPrint(servoNames[selectedServo]);
      outPrintln(F(" -> HOME (90 deg)"));
      return;
    }
    if (isNumber(line)) {
      int angle = line.toInt();
      if (setAngleChecked(selectedServo, angle)) {
        outPrint(servoNames[selectedServo]);
        outPrint(F(" -> "));
        outPrint(String(servoAngle[selectedServo]));
        outPrintln(F(" deg"));
      }
      return;
    }
    outPrintln(F("Unknown input. Type an angle (0-180), HOME, or EXIT."));
    return;
  }

  if (parseFullCommand(upper)) {
    return;
  }

  outPrintln(F("Unknown command. Type MENU for help."));
}

bool parseFullCommand(String upper) {
  bool appliedAny = false;
  int start = 0;
  int len = upper.length();

  while (start < len) {
    char letter = upper.charAt(start);
    int servoIndex = letterToServoIndex(letter);

    if (servoIndex == -1) {
      start++;
      continue;
    }

    int numStart = start + 1;
    int numEnd = numStart;
    while (numEnd < len && (isDigit(upper.charAt(numEnd)))) numEnd++;

    if (numEnd > numStart) {
      int angle = upper.substring(numStart, numEnd).toInt();
      if (setAngleChecked(servoIndex, angle)) {
        appliedAny = true;
      }
      start = numEnd;
    } else {
      start++;
    }
  }

  if (appliedAny) {
    printPositions();
  }
  return appliedAny;
}

int letterToServoIndex(char letter) {
  switch (letter) {
    case 'B': return 0; // Base
    case 'S': return 1; // Shoulder
    case 'E': return 2; // Elbow
    case 'W': return 3; // Wrist
    case 'G': return 4; // Gripper
    default:  return -1;
  }
}

bool setAngleChecked(int servoIndex, int angle) {
  if (angle < ANGLE_MIN || angle > ANGLE_MAX) {
    outPrint(F("Angle out of range (0-180): "));
    outPrintln(String(angle));
    return false;
  }
  moveServo(servoIndex, angle);
  return true;
}

void moveServo(int servoIndex, int targetAngle) {
  if (!servoAttached[servoIndex]) {
    servos[servoIndex].attach(servoPins[servoIndex]);
    servoAttached[servoIndex] = true;
  }

  int current = servoAngle[servoIndex];

  if (targetAngle > current) {
    for (int a = current; a < targetAngle; a += SERVO_STEP_DEGREES) {
      int step = min(a + (int)SERVO_STEP_DEGREES, targetAngle);
      servos[servoIndex].write(step);
      delay(SERVO_STEP_DELAY_MS);
    }
  } else if (targetAngle < current) {
    for (int a = current; a > targetAngle; a -= SERVO_STEP_DEGREES) {
      int step = max(a - (int)SERVO_STEP_DEGREES, targetAngle);
      servos[servoIndex].write(step);
      delay(SERVO_STEP_DELAY_MS);
    }
  }

  servos[servoIndex].write(targetAngle);
  servoAngle[servoIndex] = targetAngle;
  servoMoveDeadline[servoIndex] = millis() + SETTLE_TIME_MS;
}

bool isNumber(String s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isDigit(s.charAt(i))) return false;
  }
  return true;
}

// ---------------- Helper output ----------------

void printPositions() {
  outPrint(F("Positions -> "));
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    outPrint(servoNames[i]);
    outPrint(F(": "));
    outPrint(String(servoAngle[i]));
    if (i < NUM_SERVOS - 1) outPrint(F(" | "));
  }
  outPrintln(F(""));
}

void printMenu() {
  outPrintln(F("================================================"));
  outPrintln(F(" 4-DOF Robotic Arm Controller (Nano + HC-05 BT)"));
  outPrintln(F("================================================"));
  outPrintln(F("SINGLE SERVO TEST MODE:"));
  outPrintln(F("  T1=Base T2=Shoulder T3=Elbow T4=Wrist T5=Gripper"));
  outPrintln(F("  After selecting, type an angle (0-180), or HOME, or EXIT"));
  outPrintln(F(""));
  outPrintln(F("FULL CONTROL MODE (move several at once):"));
  outPrintln(F("  B<angle> S<angle> E<angle> W<angle> G<angle>"));
  outPrintln(F("  Example: B90 S90 E90 W90 G90"));
  outPrintln(F("  You can send only the ones you want, e.g: B45 E120"));
  outPrintln(F(""));
  outPrintln(F("OTHER COMMANDS:"));
  outPrintln(F("  HOMEALL -> center all servos to 90 degrees"));
  outPrintln(F("  POS     -> print current angle of all servos"));
  outPrintln(F("  MENU    -> show this menu again"));
  outPrintln(F("------------------------------------------------"));
  outPrintln(F(" Works over USB Serial AND the HC-05 Bluetooth link"));
  outPrintln(F(" at the same time - send commands from either one."));
  outPrintln(F("================================================"));
}
