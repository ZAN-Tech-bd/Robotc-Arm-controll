/*
  4-DOF Robotic Arm Controller (Arduino Nano)
  5 Servos: Base, Shoulder, Elbow, Wrist, Gripper
  All servos are 180-degree servos, assembled/centered at 90 degrees.

  Controlled over Serial Monitor (9600 baud, line ending = Newline).

  TWO MODES:
    1) SINGLE TEST MODE  - move and tune one servo at a time
    2) FULL CONTROL MODE - move all servos together with one command

  ---------------- SERIAL COMMANDS ----------------
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
                          in one command (order fixed: B S E W G).
                          You don't need to include all five letters;
                          only the ones you type will move, e.g: B45 E120
  HOMEALL              -> send all 5 servos to 90 degrees (center/assembly pose)
  POS                  -> print the current angle of all servos
  --------------------------------------------------
*/

#include <Servo.h>

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
int           servoAngle[NUM_SERVOS]; // last commanded angle of each servo

const int HOME_ANGLE = 90; // matches how the arm was physically assembled
const int ANGLE_MIN  = 0;
const int ANGLE_MAX  = 180;

// ---------- Mode state ----------
bool    singleTestMode   = false;
int8_t  selectedServo    = -1; // index into servos[], -1 = none selected

String inputLine = "";

void setup() {
  Serial.begin(9600);

  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(servoPins[i]);
    servoAngle[i] = HOME_ANGLE;
    servos[i].write(HOME_ANGLE); // start every servo at its assembled 90-degree position
  }

  printMenu();
}

void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();
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
    Serial.println(F("All servos moved to HOME (90 degrees)."));
    return;
  }

  // --- Enter single-servo test mode: T1..T5 ---
  if (upper.length() == 2 && upper.charAt(0) == 'T' && isDigit(upper.charAt(1))) {
    int idx = upper.charAt(1) - '1'; // T1 -> 0
    if (idx >= 0 && idx < NUM_SERVOS) {
      singleTestMode = true;
      selectedServo = idx;
      Serial.print(F("-- TEST MODE: "));
      Serial.print(servoNames[idx]);
      Serial.println(F(" selected. Type an angle (0-180) and press Enter."));
      Serial.println(F("   Type HOME to center it, EXIT to leave test mode."));
    } else {
      Serial.println(F("Invalid servo number. Use T1 to T5."));
    }
    return;
  }

  // --- While inside single test mode ---
  if (singleTestMode) {
    if (upper == "EXIT") {
      Serial.print(F("Exiting test mode for "));
      Serial.println(servoNames[selectedServo]);
      singleTestMode = false;
      selectedServo = -1;
      return;
    }
    if (upper == "HOME") {
      moveServo(selectedServo, HOME_ANGLE);
      Serial.print(servoNames[selectedServo]);
      Serial.println(F(" -> HOME (90 deg)"));
      return;
    }
    if (isNumber(line)) {
      int angle = line.toInt();
      if (setAngleChecked(selectedServo, angle)) {
        Serial.print(servoNames[selectedServo]);
        Serial.print(F(" -> "));
        Serial.print(servoAngle[selectedServo]);
        Serial.println(F(" deg"));
      }
      return;
    }
    Serial.println(F("Unknown input. Type an angle (0-180), HOME, or EXIT."));
    return;
  }

  // --- Full control mode: e.g. "B90 S90 E90 W90 G90" ---
  if (parseFullCommand(upper)) {
    return;
  }

  Serial.println(F("Unknown command. Type MENU for help."));
}

// Parses tokens like B90, S45, E120, W10, G0 from one line.
// Returns true if at least one valid token was found and applied.
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
    Serial.print(F("Angle out of range (0-180): "));
    Serial.println(angle);
    return false;
  }
  moveServo(servoIndex, angle);
  return true;
}

void moveServo(int servoIndex, int angle) {
  servos[servoIndex].write(angle);
  servoAngle[servoIndex] = angle;
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
  Serial.print(F("Positions -> "));
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    Serial.print(servoNames[i]);
    Serial.print(F(": "));
    Serial.print(servoAngle[i]);
    if (i < NUM_SERVOS - 1) Serial.print(F(" | "));
  }
  Serial.println();
}

void printMenu() {
  Serial.println(F("================================================"));
  Serial.println(F(" 4-DOF Robotic Arm Controller (Arduino Nano)"));
  Serial.println(F("================================================"));
  Serial.println(F("SINGLE SERVO TEST MODE:"));
  Serial.println(F("  T1 = Base, T2 = Shoulder, T3 = Elbow, T4 = Wrist, T5 = Gripper"));
  Serial.println(F("  After selecting, type an angle (0-180), or HOME, or EXIT"));
  Serial.println();
  Serial.println(F("FULL CONTROL MODE (move several at once):"));
  Serial.println(F("  B<angle> S<angle> E<angle> W<angle> G<angle>"));
  Serial.println(F("  Example: B90 S90 E90 W90 G90"));
  Serial.println(F("  You can send only the ones you want, e.g: B45 E120"));
  Serial.println();
  Serial.println(F("OTHER COMMANDS:"));
  Serial.println(F("  HOMEALL -> center all servos to 90 degrees"));
  Serial.println(F("  POS     -> print current angle of all servos"));
  Serial.println(F("  MENU    -> show this menu again"));
  Serial.println(F("================================================"));
}
