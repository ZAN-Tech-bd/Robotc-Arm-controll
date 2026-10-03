/*
  4-Servo Robotic Arm Controller (Arduino Nano)
  4 Servos: Base, Shoulder, Elbow, Gripper (no separate wrist joint)
  All servos are 180-degree servos, assembled/centered at 90 degrees.

  Controlled over Serial Monitor (9600 baud, line ending = Newline).
  This is the 4-servo sibling of firmware/4dof-arm and firmware/6dof-arm -
  same serial protocol and behavior, just one joint fewer (no wrist). The
  ZAN Tech PC app (pc-app/) talks to any of the three depending on which
  "Arm Type" you pick in its menu.

  TWO MODES:
    1) SINGLE TEST MODE  - move and tune one servo at a time
    2) FULL CONTROL MODE - move all servos together with one command

  ---------------- SERIAL COMMANDS ----------------
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

#include <Servo.h>

// ---------- Pin configuration ----------
const uint8_t PIN_BASE     = 3;
const uint8_t PIN_SHOULDER = 5;
const uint8_t PIN_ELBOW    = 6;
const uint8_t PIN_GRIPPER  = 9;

const uint8_t NUM_SERVOS = 4;

Servo servos[NUM_SERVOS];
const uint8_t servoPins[NUM_SERVOS] = { PIN_BASE, PIN_SHOULDER, PIN_ELBOW, PIN_GRIPPER };
const char*   servoNames[NUM_SERVOS] = { "Base", "Shoulder", "Elbow", "Gripper" };
int           servoAngle[NUM_SERVOS];     // last commanded angle of each servo
bool          servoAttached[NUM_SERVOS];  // is this servo currently receiving a PWM signal?
unsigned long servoMoveDeadline[NUM_SERVOS]; // when it's safe to stop sending PWM (ms)

const int HOME_ANGLE = 90; // matches how the arm was physically assembled
const int ANGLE_MIN  = 0;
const int ANGLE_MAX  = 180;

// Once a servo reaches its target, we stop sending it a PWM pulse. A servo that keeps
// receiving a signal can twitch/drift on its own from electrical noise or a slightly
// unstable power supply - it looks like "the arm moves by itself" even though nothing
// sent a command. Detaching after it settles keeps it perfectly still until the next
// real command re-attaches it and moves it. Set to false if your arm instead needs to
// stay powered to resist gravity (e.g. a heavy shoulder joint sagging when idle).
const bool          DETACH_WHEN_IDLE   = true;
const unsigned long SETTLE_TIME_MS     = 500; // time to let the servo physically get there

// Smooth motion: instead of jumping straight to the target angle, each servo steps
// there a few degrees at a time (0 -> 5 -> 10 -> 15 ... -> 90), which looks much nicer
// and is gentler on the gears than a sudden jump.
const uint8_t        SERVO_STEP_DEGREES = 5;   // how many degrees to move per step
const unsigned long  SERVO_STEP_DELAY_MS = 25; // pause between steps (ms)

// ---------- Mode state ----------
bool    singleTestMode   = false;
int8_t  selectedServo    = -1; // index into servos[], -1 = none selected

String inputLine = "";

void setup() {
  Serial.begin(9600);

  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(servoPins[i]);
    servoAttached[i] = true;
    servoAngle[i] = HOME_ANGLE;
    servos[i].write(HOME_ANGLE); // start every servo at its assembled 90-degree position
    servoMoveDeadline[i] = millis() + SETTLE_TIME_MS;
  }

  // Discard any garbage bytes that showed up on the serial line while the
  // board was powering up / the USB-serial chip was enumerating, so they
  // can't be mistaken for a command.
  delay(50);
  while (Serial.available() > 0) Serial.read();

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

  releaseSettledServos();
}

// Stops sending a PWM pulse to any servo that reached its target angle a
// while ago, so an idle arm holds perfectly still instead of twitching.
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

  // --- Enter single-servo test mode: T1..T4 ---
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
      Serial.println(F("Invalid servo number. Use T1 to T4."));
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

  // --- Full control mode: e.g. "B90 S90 E90 G90" ---
  if (parseFullCommand(upper)) {
    return;
  }

  Serial.println(F("Unknown command. Type MENU for help."));
}

// Parses tokens like B90, S45, E120, G0 from one line.
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
    case 'G': return 3; // Gripper
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

// Moves one servo smoothly from its current angle to the target, a few degrees
// at a time (see SERVO_STEP_DEGREES) instead of snapping straight there.
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

  servos[servoIndex].write(targetAngle); // make sure it lands exactly on target
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
  Serial.println(F(" 4-Servo Robotic Arm Controller (Arduino Nano)"));
  Serial.println(F("================================================"));
  Serial.println(F("SINGLE SERVO TEST MODE:"));
  Serial.println(F("  T1=Base T2=Shoulder T3=Elbow T4=Gripper"));
  Serial.println(F("  After selecting, type an angle (0-180), or HOME, or EXIT"));
  Serial.println();
  Serial.println(F("FULL CONTROL MODE (move several at once):"));
  Serial.println(F("  B<angle> S<angle> E<angle> G<angle>"));
  Serial.println(F("  Example: B90 S90 E90 G90"));
  Serial.println(F("  You can send only the ones you want, e.g: B45 E120"));
  Serial.println();
  Serial.println(F("OTHER COMMANDS:"));
  Serial.println(F("  HOMEALL -> center all servos to 90 degrees"));
  Serial.println(F("  POS     -> print current angle of all servos"));
  Serial.println(F("  MENU    -> show this menu again"));
  Serial.println(F("================================================"));
}
