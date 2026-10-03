/*
  4-Servo Robotic Arm Controller - ESP32 Bluetooth version
  4 Servos: Base, Shoulder, Elbow, Gripper (no separate wrist joint)
  All servos are 180-degree servos, assembled/centered at 90 degrees.

  This is the ESP32 sibling of firmware/4servo-arm/4servo-arm.ino - same
  serial protocol, same menu, same behavior. No HC-05 module needed: the
  ESP32 has Bluetooth built in, so this uses its classic Bluetooth SPP
  (serial-over-Bluetooth) to talk to a phone app (e.g. "Serial Bluetooth
  Terminal") or a PC, exactly like a wireless USB cable. USB Serial still
  works at the same time, for debugging in the Arduino IDE.

  ---------------- REQUIRED LIBRARIES ----------------
  - "ESP32Servo" by Kevin Harrington / John K. Bennett (Library Manager)
    The plain Servo.h from the AVR core does not work on ESP32 - this
    library provides a drop-in Servo class that does.
  - Bluetooth Serial: built into the ESP32 Arduino core (BluetoothSerial.h),
    nothing extra to install. Make sure Tools > Board is an ESP32 board and
    Tools > Partition Scheme includes Bluetooth (the default usually does).

  ---------------- WIRING (ESP32 DevKit) ----------------
  Base      -> GPIO25
  Shoulder  -> GPIO26
  Elbow     -> GPIO27
  Gripper   -> GPIO32
  All servos: Signal to the GPIO above, VCC to an external 5V supply (NOT
  the ESP32's 3.3V/5V pin - servos draw too much for the board to supply),
  GND shared between the servos' supply and the ESP32's GND.

  ---------------- PAIRING ----------------
  Flash this, then on your phone/PC open Bluetooth settings and pair with
  the device named "ZANTECH_ARM_4SERVO" (no PIN needed by default). Then
  open a serial Bluetooth terminal app and connect to it - it behaves like
  any other serial port once paired.

  ---------------- SERIAL / BLUETOOTH COMMANDS ----------------
  MENU                 -> show the menu again

  --- Single servo test mode ---
  T1                   -> select servo 1 (Base)      for testing
  T2                   -> select servo 2 (Shoulder)  for testing
  T3                   -> select servo 3 (Elbow)     for testing
  T4                   -> select servo 4 (Gripper)   for testing
  <number 0-180>       -> once a servo is selected (T1..T4), just type an
                          angle and press Enter to move ONLY that servo
  HOME                 -> send the selected servo back to 90 degrees
  EXIT                 -> leave single test mode

  --- Full control mode (all 4 servos in one line) ---
  B90 S90 E90 G90      -> set Base, Shoulder, Elbow, Gripper angles in one
                          command (order doesn't matter). You don't need to
                          include all four letters; only the ones you type
                          will move, e.g: B45 E120
  HOMEALL              -> send all 4 servos to 90 degrees (center/assembly pose)
  POS                  -> print the current angle of all servos
  --------------------------------------------------
*/

#include <ESP32Servo.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled - pick an ESP32 board and the default partition scheme in the Arduino IDE.
#endif

BluetoothSerial SerialBT;
const char* BT_DEVICE_NAME = "ZANTECH_ARM_4SERVO";

// ---------- Pin configuration ----------
const uint8_t PIN_BASE     = 25;
const uint8_t PIN_SHOULDER = 26;
const uint8_t PIN_ELBOW    = 27;
const uint8_t PIN_GRIPPER  = 32;

const uint8_t NUM_SERVOS = 4;

Servo servos[NUM_SERVOS];
const uint8_t servoPins[NUM_SERVOS] = { PIN_BASE, PIN_SHOULDER, PIN_ELBOW, PIN_GRIPPER };
const char*   servoNames[NUM_SERVOS] = { "Base", "Shoulder", "Elbow", "Gripper" };
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

void attachServo(uint8_t i) {
  servos[i].setPeriodHertz(50);          // standard 50Hz servo PWM
  servos[i].attach(servoPins[i], 500, 2400);
  servoAttached[i] = true;
}

void setup() {
  Serial.begin(115200);
  SerialBT.begin(BT_DEVICE_NAME);

  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    attachServo(i);
    servoAngle[i] = HOME_ANGLE;
    servos[i].write(HOME_ANGLE);
    servoMoveDeadline[i] = millis() + SETTLE_TIME_MS;
  }

  delay(50);
  while (Serial.available() > 0) Serial.read();
  while (SerialBT.available() > 0) SerialBT.read();

  printMenu();
}

void loop() {
  readStreamInto(Serial);
  readStreamInto(SerialBT);
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
  SerialBT.print(s);
}
void outPrintln(const String &s) {
  Serial.println(s);
  SerialBT.println(s);
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
    outPrintln("All servos moved to HOME (90 degrees).");
    return;
  }

  if (upper.length() == 2 && upper.charAt(0) == 'T' && isDigit(upper.charAt(1))) {
    int idx = upper.charAt(1) - '1';
    if (idx >= 0 && idx < NUM_SERVOS) {
      singleTestMode = true;
      selectedServo = idx;
      outPrint("-- TEST MODE: ");
      outPrint(servoNames[idx]);
      outPrintln(" selected. Type an angle (0-180) and press Enter.");
      outPrintln("   Type HOME to center it, EXIT to leave test mode.");
    } else {
      outPrintln("Invalid servo number. Use T1 to T4.");
    }
    return;
  }

  if (singleTestMode) {
    if (upper == "EXIT") {
      outPrint("Exiting test mode for ");
      outPrintln(servoNames[selectedServo]);
      singleTestMode = false;
      selectedServo = -1;
      return;
    }
    if (upper == "HOME") {
      moveServo(selectedServo, HOME_ANGLE);
      outPrint(servoNames[selectedServo]);
      outPrintln(" -> HOME (90 deg)");
      return;
    }
    if (isNumber(line)) {
      int angle = line.toInt();
      if (setAngleChecked(selectedServo, angle)) {
        outPrint(servoNames[selectedServo]);
        outPrint(" -> ");
        outPrint(String(servoAngle[selectedServo]));
        outPrintln(" deg");
      }
      return;
    }
    outPrintln("Unknown input. Type an angle (0-180), HOME, or EXIT.");
    return;
  }

  if (parseFullCommand(upper)) {
    return;
  }

  outPrintln("Unknown command. Type MENU for help.");
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
    case 'G': return 3; // Gripper
    default:  return -1;
  }
}

bool setAngleChecked(int servoIndex, int angle) {
  if (angle < ANGLE_MIN || angle > ANGLE_MAX) {
    outPrint("Angle out of range (0-180): ");
    outPrintln(String(angle));
    return false;
  }
  moveServo(servoIndex, angle);
  return true;
}

void moveServo(int servoIndex, int targetAngle) {
  if (!servoAttached[servoIndex]) {
    attachServo(servoIndex);
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
  outPrint("Positions -> ");
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    outPrint(servoNames[i]);
    outPrint(": ");
    outPrint(String(servoAngle[i]));
    if (i < NUM_SERVOS - 1) outPrint(" | ");
  }
  outPrintln("");
}

void printMenu() {
  outPrintln("================================================");
  outPrintln(" 4-Servo Robotic Arm Controller (ESP32 Bluetooth)");
  outPrintln("================================================");
  outPrintln("SINGLE SERVO TEST MODE:");
  outPrintln("  T1=Base T2=Shoulder T3=Elbow T4=Gripper");
  outPrintln("  After selecting, type an angle (0-180), or HOME, or EXIT");
  outPrintln("");
  outPrintln("FULL CONTROL MODE (move several at once):");
  outPrintln("  B<angle> S<angle> E<angle> G<angle>");
  outPrintln("  Example: B90 S90 E90 G90");
  outPrintln("  You can send only the ones you want, e.g: B45 E120");
  outPrintln("");
  outPrintln("OTHER COMMANDS:");
  outPrintln("  HOMEALL -> center all servos to 90 degrees");
  outPrintln("  POS     -> print current angle of all servos");
  outPrintln("  MENU    -> show this menu again");
  outPrintln("------------------------------------------------");
  outPrintln(" Works over USB Serial AND Bluetooth SPP at the");
  outPrintln(" same time - pair with \"ZANTECH_ARM_4SERVO\".");
  outPrintln("================================================");
}
