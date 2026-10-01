# ZAN Tech Robotic Arm Controller — 4-DOF & 6-DOF

**Build a robot arm, flash one file, double-click one app, drag some sliders.**
This repo has everything you need: Arduino firmware for a 5-servo (4-DOF) arm *or* a
6-servo (6-DOF) arm, plus a single PC app with a modern slider interface that controls
either one — no coding and no typing commands required to actually use the arm.

```
┌─────────────────────────┐        USB Serial        ┌───────────────────────────┐
│   ZAN Tech PC App        │ ◄─────────────────────► │   Arduino Nano             │
│   (Python, double-click) │   "B90\n" "S45\n" etc.   │   drives 5 or 6 servos     │
│   - pick 4-DOF or 6-DOF │                          │   0-180° each, smooth move │
│   - sliders per joint    │                          │                            │
│   - Home All / live log  │                          │                            │
└─────────────────────────┘                           └───────────────────────────┘
```

![ZAN Tech Robotic Arm Controller - main screen](docs/screenshots/gui-main.png)

---

## 1. Which arm do you have?

| | 4-DOF Arm | 6-DOF Arm |
|---|---|---|
| Servos | 5 (Base, Shoulder, Elbow, Wrist, Gripper) | 6 (Base, Shoulder, Elbow, Wrist **Pitch**, Wrist **Roll**, Gripper) |
| Firmware file | [`firmware/4dof-arm/4dof-arm.ino`](firmware/4dof-arm/4dof-arm.ino) | [`firmware/6dof-arm/6dof-arm.ino`](firmware/6dof-arm/6dof-arm.ino) |
| Good for | Simpler builds, cheaper kits, beginners | Arms that also need to rotate the gripper (wrist roll) |

Both run on the same hardware (**Arduino Nano**), use the same serial protocol style, and
are controlled by the **same PC app** — you just tell the app which one you built.

---

## 2. What's in this repo

```
4-Dof-Robotc-Arm-controll/
├── firmware/
│   ├── 4dof-arm/
│   │   └── 4dof-arm.ino        # Flash this if you built the 5-servo arm
│   └── 6dof-arm/
│       └── 6dof-arm.ino        # Flash this if you built the 6-servo arm
├── pc-app/
│   ├── assets/
│   │   ├── zantech_logo.png        # Header logo shown in the app
│   │   └── zantech_logo_icon.png   # App window icon
│   ├── arm_controller_gui.py       # The PC app (controls either arm)
│   ├── requirements.txt            # One dependency: pyserial
│   └── Run Arm Controller.bat      # Double-click this - no terminal needed
├── docs/
│   └── screenshots/                # Images used in this README
└── README.md                       # You are here
```

---

## 3. Parts list

- 1x **Arduino Nano**
- 5x servos (4-DOF arm) **or** 6x servos (6-DOF arm) — standard 180° hobby servos
- An external **5V power supply** for the servos if you have more than 2-3 of them (the
  Nano's own USB power usually isn't enough — servos will twitch or the board will reset)
- Jumper wires + a breadboard (or your arm kit's wiring harness)

---

## 4. Wiring

All servos are 3-wire (Signal, VCC/5V, GND). **Share ground** between the Nano and any
external power supply, or the servos will jitter or not respond at all.

### 4-DOF Arm (5 servos)

| Servo | Signal Pin (Nano) |
|---|---|
| Base | **D3** |
| Shoulder | **D5** |
| Elbow | **D6** |
| Wrist | **D9** |
| Gripper | **D10** |

```
                         ┌───────────────────────────┐
                         │       Arduino Nano        │
   BASE SERVO             │                           │
   ┌───────────────┐     │                           │
   │ Signal        ├─────┤ D3                        │
   │ VCC (5V)      ├─────┤ 5V (external supply       │
   │ GND           ├─────┤ GND  recommended)          │
   └───────────────┘     │                           │
   SHOULDER SERVO         │                           │
   ┌───────────────┐     │                           │
   │ Signal        ├─────┤ D5                        │
   │ VCC / GND     ├─────┤ 5V / GND                  │
   └───────────────┘     │                           │
   ELBOW SERVO             │                           │
   ┌───────────────┐     │                           │
   │ Signal        ├─────┤ D6                        │
   │ VCC / GND     ├─────┤ 5V / GND                  │
   └───────────────┘     │                           │
   WRIST SERVO             │                           │
   ┌───────────────┐     │                           │
   │ Signal        ├─────┤ D9                        │
   │ VCC / GND     ├─────┤ 5V / GND                  │
   └───────────────┘     │                           │
   GRIPPER SERVO           │                           │
   ┌───────────────┐     │                           │
   │ Signal        ├─────┤ D10                       │
   │ VCC / GND     ├─────┤ 5V / GND                  │
   └───────────────┘     └───────────────────────────┘
```

### 6-DOF Arm (6 servos)

| Servo | Signal Pin (Nano) |
|---|---|
| Base | **D3** |
| Shoulder | **D5** |
| Elbow | **D6** |
| Wrist Pitch (up/down) | **D9** |
| Wrist Roll (rotate gripper) | **D10** |
| Gripper | **D11** |

```
                         ┌───────────────────────────┐
                         │       Arduino Nano        │
   BASE → D3              │                           │
   SHOULDER → D5           │                           │
   ELBOW → D6               │                           │
   WRIST PITCH → D9         │                           │
   WRIST ROLL → D10         │                           │
   GRIPPER → D11            │                           │
                         │  (same Signal/5V/GND       │
                         │   wiring pattern as the    │
                         │   4-DOF table above)       │
                         └───────────────────────────┘
```

---

## 5. Flashing the firmware

1. Install the **Arduino IDE**.
2. Plug the Nano into your PC with USB.
3. Open the right `.ino` for your arm:
   - 4-DOF → `firmware/4dof-arm/4dof-arm.ino`
   - 6-DOF → `firmware/6dof-arm/6dof-arm.ino`
4. Select **Board: Arduino Nano** and the correct **Port**.
5. Click **Upload**.

No extra libraries needed — `Servo.h` ships with the Arduino IDE.

Once uploaded, every servo snaps to 90° (home), settles for half a second, then the
firmware stops sending it a signal until you give it a real command — so the arm holds
still and never drifts or twitches on its own while idle. When you do send a new angle,
the servo doesn't jump — it sweeps there smoothly in 5° steps.

---

## 6. Running the PC app

### Easiest way — just double-click it

1. Install **Python 3** from [python.org](https://www.python.org/downloads/) if you don't
   have it (tick **"Add Python to PATH"** during install).
2. Open the `pc-app` folder.
3. Double-click **`Run Arm Controller.bat`**.

That's it — no terminal, no typing `python` commands. The first run installs the one
required package (`pyserial`) automatically, then opens the app window. If something's
wrong (Python missing, etc.) it tells you instead of silently doing nothing.

> If double-clicking it does nothing and no window appears, check `crash_log.txt` next
> to it for the error details.

### Manual way (optional)

```bash
cd pc-app
pip install -r requirements.txt
python arm_controller_gui.py
```

### There are two ways to control the arm — use either, or both

**A) Sliders** (the easy way — recommended for everyone):

1. **Arm Type** — pick "4-DOF Arm" or "6-DOF Arm" to match the firmware you flashed.
   The slider panel updates automatically.
2. **Port** — pick your Arduino's port. It's labeled with its description (e.g.
   `COM3 - USB-SERIAL CH340`) so you can tell it apart from unrelated ports like your
   PC's built-in `COM1`.
3. Click **Connect**. The status dot turns green when it's talking to the arm. If the
   arm doesn't reply to a quick test command within 1.5 seconds, the app warns you —
   that usually means the wrong port or the wrong Arm Type was selected.
4. **Drag any slider** — that joint moves on the real arm immediately.
5. **Home All** — snaps every joint back to 90° (center).
6. **Refresh Positions** — asks the arm what angle every servo is currently at.

**B) Typed commands** (the power-user way — for scripting, testing, or fine control):

Click **"▸ Show serial console"** at the bottom of the app to reveal a command box —
type any firmware command directly (`MENU`, `POS`, `HOMEALL`, `T1`, `EXIT`, `B45 E120`,
etc.) and press Enter or click Send. This works exactly like the Arduino IDE's Serial
Monitor, but without leaving the app, and you can see the arm's replies live. Typing a
servo command here (e.g. `B45`) also moves that slider to match, so the sliders and the
console never fall out of sync with each other.

![Serial console panel open, with a typed command](docs/screenshots/gui-console.png)

> You can also use the **real Arduino IDE Serial Monitor** instead (9600 baud, line
> ending set to "Newline") if you'd rather not use the PC app at all — it's the exact
> same protocol either way. The full command list for each arm is below.

---

## 7. Serial command reference

Every command below works identically whether you type it into the app's built-in
serial console or the Arduino IDE's Serial Monitor — the firmware doesn't know or care
which one sent it.

**4-DOF Arm:**
```
MENU                  -> show the full command list
B90 S90 E90 W90 G90   -> move all 5 servos together in one line
B45 E120              -> move only the servos you mention
HOMEALL               -> center every servo at 90 degrees
POS                   -> print the current angle of every servo
T1                    -> select servo 1 (Base) to test by itself (T1-T5)
HOME                  -> (while in T1-T5 test mode) center just that one servo
EXIT                  -> leave test mode
```

**6-DOF Arm:**
```
MENU                        -> show the full command list
B90 S90 E90 P90 R90 G90     -> move all 6 servos together in one line
B45 P120                    -> move only the servos you mention
HOMEALL                     -> center every servo at 90 degrees
POS                         -> print the current angle of every servo
T1                          -> select servo 1 (Base) to test by itself (T1-T6)
HOME                        -> (while in T1-T6 test mode) center just that one servo
EXIT                        -> leave test mode
```

| Command | 4-DOF letters | 6-DOF letters |
|---|---|---|
| Base | `B` | `B` |
| Shoulder | `S` | `S` |
| Elbow | `E` | `E` |
| Wrist | `W` | — |
| Wrist Pitch | — | `P` |
| Wrist Roll | — | `R` |
| Gripper | `G` | `G` |

---

## 8. Troubleshooting

| Symptom | Fix |
|---|---|
| App says "Port is busy" | Close the Arduino IDE's Serial Monitor, or any other copy of this app, or any other serial tool — only one program can use a COM port at a time. |
| Connected, but arm doesn't move | You probably picked the wrong port (e.g. your PC's built-in COM1) or the wrong Arm Type. The app should warn you about this automatically within ~1.5s of connecting. |
| No port appears in the dropdown | Click Refresh. Make sure the USB cable is a *data* cable, not charge-only. |
| Servos twitch, reset, or seem to "move by themselves" | Power the servos from an external 5V supply (not the Nano's USB power) and share ground between the supply and the Nano. The firmware already detaches idle servos to prevent signal jitter — this is almost always a power issue. |
| Arm moves the wrong joint | Double-check the wiring table in Section 4 — each servo's signal wire must go to its matching pin, and make sure you flashed the firmware that matches your arm (4-DOF vs 6-DOF). |
| Your arm needs to stay powered while idle (e.g. shoulder sags under gravity) | Open the `.ino` file for your arm and set `DETACH_WHEN_IDLE` to `false`, then re-upload. |

---

## 9. License

Part of the ZAN Tech open-source robotics project collection — free to build, modify, and
use for learning, hobby, and personal robotics projects.
