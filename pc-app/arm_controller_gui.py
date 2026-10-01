"""
4-DOF Robotic Arm Controller GUI
---------------------------------
A simple desktop app (Tkinter, built into Python) that talks to the
Arduino Nano arm firmware over USB serial and lets you drag sliders
to move all 5 servos together: Base, Shoulder, Elbow, Wrist, Gripper.

How it works:
  - Pick your Arduino's serial port from the dropdown and click Connect.
  - Drag any slider - the app sends that one servo's new angle to the
    arm immediately (e.g. "B90\\n"), matching the firmware's serial
    protocol in 4-Dof-Robotc-Arm-controll.ino.
  - "Home All" sends HOMEALL, "Refresh Positions" sends POS and shows
    the arm's reply in the log box at the bottom.

Install the one dependency, then run the app:
    pip install -r requirements.txt
    python arm_controller_gui.py
"""

import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

import serial
import serial.tools.list_ports

BAUD_RATE = 9600
SLIDER_MIN = 0
SLIDER_MAX = 180
HOME_ANGLE = 90

# Must match the single-letter codes the firmware expects (see the .ino menu).
SERVOS = [
    ("Base", "B"),
    ("Shoulder", "S"),
    ("Elbow", "E"),
    ("Wrist", "W"),
    ("Gripper", "G"),
]


class ArmControllerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ZAN Tech - Robotic Arm Controller")
        self.root.geometry("520x560")
        self.root.resizable(False, False)

        self.serial_conn = None
        self.read_thread = None
        self.stop_reading = threading.Event()
        self.incoming_queue = queue.Queue()
        self._port_label_to_device = {}
        self._got_any_reply = False

        self.sliders = {}
        self.value_labels = {}

        self._build_ui()
        self._refresh_ports()
        self._poll_incoming_queue()

    # ------------------------------------------------------------------ UI
    def _build_ui(self):
        pad = {"padx": 8, "pady": 6}

        # --- Connection row ---
        conn_frame = ttk.LabelFrame(self.root, text="Connection")
        conn_frame.pack(fill="x", **pad)

        ttk.Label(conn_frame, text="Port:").grid(row=0, column=0, padx=6, pady=6)
        self.port_combo = ttk.Combobox(conn_frame, state="readonly", width=32)
        self.port_combo.grid(row=0, column=1, padx=6, pady=6)

        ttk.Button(conn_frame, text="Refresh", command=self._refresh_ports).grid(
            row=0, column=2, padx=6, pady=6
        )
        self.connect_btn = ttk.Button(
            conn_frame, text="Connect", command=self._toggle_connection
        )
        self.connect_btn.grid(row=0, column=3, padx=6, pady=6)

        self.status_label = ttk.Label(conn_frame, text="Disconnected", foreground="red")
        self.status_label.grid(row=0, column=4, padx=10, pady=6)

        # --- Sliders ---
        servo_frame = ttk.LabelFrame(self.root, text="Servos (0-180 degrees)")
        servo_frame.pack(fill="x", **pad)

        for row, (name, letter) in enumerate(SERVOS):
            ttk.Label(servo_frame, text=f"{name} ({letter})", width=14).grid(
                row=row, column=0, padx=6, pady=8, sticky="w"
            )

            value_label = ttk.Label(servo_frame, text=str(HOME_ANGLE), width=4)
            value_label.grid(row=row, column=2, padx=6, pady=8)
            self.value_labels[letter] = value_label

            slider = ttk.Scale(
                servo_frame,
                from_=SLIDER_MIN,
                to=SLIDER_MAX,
                orient="horizontal",
                length=300,
                command=lambda val, letter=letter: self._on_slider_move(letter, val),
            )
            # Set the label text directly instead of slider.set(), which would
            # fire the command callback above and send a spurious "B90" etc.
            # to the arm before Connect has even been clicked.
            slider.configure(value=HOME_ANGLE)
            slider.grid(row=row, column=1, padx=6, pady=8)
            self.sliders[letter] = slider

        # --- Quick actions ---
        action_frame = ttk.Frame(self.root)
        action_frame.pack(fill="x", **pad)

        ttk.Button(action_frame, text="Home All", command=self._send_home_all).pack(
            side="left", padx=6
        )
        ttk.Button(
            action_frame, text="Refresh Positions", command=self._send_get_positions
        ).pack(side="left", padx=6)

        # --- Log console ---
        log_frame = ttk.LabelFrame(self.root, text="Arm Log")
        log_frame.pack(fill="both", expand=True, **pad)

        self.log_box = tk.Text(log_frame, height=12, state="disabled", wrap="word")
        self.log_box.pack(fill="both", expand=True, padx=6, pady=6)

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ------------------------------------------------------------ Serial
    def _refresh_ports(self):
        found = list(serial.tools.list_ports.comports())
        # Show "COM3 - USB-SERIAL CH340" style labels so you can tell the real
        # Arduino apart from unrelated ports (e.g. a motherboard's built-in
        # COM1, which has no USB description and isn't your Arduino).
        labels = [f"{p.device} - {p.description}" for p in found]
        self._port_label_to_device = {label: p.device for label, p in zip(labels, found)}

        self.port_combo["values"] = labels
        if labels and not self.port_combo.get():
            self.port_combo.current(0)

        if not found:
            self._log("No serial ports found. Plug in the Arduino and click Refresh.")

    def _selected_device(self):
        label = self.port_combo.get()
        return self._port_label_to_device.get(label, label.split(" - ")[0])

    def _toggle_connection(self):
        if self.serial_conn is None:
            self._connect()
        else:
            self._disconnect()

    def _connect(self):
        port = self._selected_device()
        if not port:
            messagebox.showwarning("No port selected", "Pick a serial port first.")
            return
        try:
            self.serial_conn = serial.Serial(port, BAUD_RATE, timeout=0.2)
        except serial.SerialException as exc:
            self.serial_conn = None
            if "PermissionError" in str(exc) or "Access is denied" in str(exc):
                messagebox.showerror(
                    "Port is busy",
                    f"{port} is already being used by another program.\n\n"
                    "Common causes:\n"
                    "  - The Arduino IDE's Serial Monitor/Plotter is open - close it.\n"
                    "  - Another copy of this app is already running - close it.\n"
                    "  - Another serial tool (PuTTY, etc.) has the port open.\n\n"
                    "Close whatever else is using it, then click Connect again.",
                )
            else:
                messagebox.showerror("Could not connect", str(exc))
            return

        # Give the Arduino a moment to reset after the port opens.
        time.sleep(2)

        self._got_any_reply = False
        self.stop_reading.clear()
        self.read_thread = threading.Thread(target=self._read_loop, daemon=True)
        self.read_thread.start()

        self.status_label.config(text=f"Connected ({port})", foreground="green")
        self.connect_btn.config(text="Disconnect")
        self._log(f"Connected to {port} at {BAUD_RATE} baud.")

        # Ask the arm to prove it's actually there - if nothing on the other
        # end of this port understands "POS", you probably picked the wrong
        # port (e.g. a PC's built-in COM1, which isn't the Arduino).
        self._send("POS")
        self.root.after(1500, self._check_arm_responded)

    def _check_arm_responded(self):
        if self.serial_conn is None:
            return  # already disconnected
        if not self._got_any_reply:
            messagebox.showwarning(
                "No response from the arm",
                f"Connected to {self._selected_device()}, but it never replied to a "
                "test command.\n\nThis usually means you picked the wrong port (for "
                "example a PC's built-in COM1 instead of the Arduino's USB port). "
                "Check Windows Device Manager -> Ports (COM & LPT) for the real "
                "Arduino port, then reconnect.",
            )

    def _disconnect(self):
        self.stop_reading.set()
        if self.serial_conn is not None:
            try:
                self.serial_conn.close()
            except serial.SerialException:
                pass
        self.serial_conn = None
        self.status_label.config(text="Disconnected", foreground="red")
        self.connect_btn.config(text="Connect")
        self._log("Disconnected.")

    def _read_loop(self):
        """Runs on a background thread; never touches the UI directly."""
        while not self.stop_reading.is_set() and self.serial_conn is not None:
            try:
                line = self.serial_conn.readline()
            except serial.SerialException:
                break
            if line:
                text = line.decode(errors="replace").strip()
                if text:
                    self.incoming_queue.put(text)

    def _poll_incoming_queue(self):
        """Runs on the main thread; safe to touch the UI here."""
        try:
            while True:
                text = self.incoming_queue.get_nowait()
                self._got_any_reply = True
                self._log(f"< {text}")
        except queue.Empty:
            pass
        self.root.after(100, self._poll_incoming_queue)

    def _send(self, command):
        if self.serial_conn is None:
            return
        try:
            self.serial_conn.write((command + "\n").encode())
            self._log(f"> {command}")
        except serial.SerialException as exc:
            messagebox.showerror("Send failed", str(exc))
            self._disconnect()

    # ------------------------------------------------------------ Actions
    def _on_slider_move(self, letter, value):
        angle = int(float(value))
        self.value_labels[letter].config(text=str(angle))
        self._send(f"{letter}{angle}")

    def _send_home_all(self):
        for letter in self.sliders:
            self.sliders[letter].set(HOME_ANGLE)
            self.value_labels[letter].config(text=str(HOME_ANGLE))
        self._send("HOMEALL")

    def _send_get_positions(self):
        self._send("POS")

    def _log(self, text):
        self.log_box.config(state="normal")
        self.log_box.insert("end", text + "\n")
        self.log_box.see("end")
        self.log_box.config(state="disabled")

    def _on_close(self):
        self._disconnect()
        self.root.destroy()


def main():
    app_root = tk.Tk()
    ArmControllerApp(app_root)
    app_root.mainloop()


if __name__ == "__main__":
    # When launched by double-clicking Run Arm Controller.bat (via pythonw),
    # there is no console to show errors in - so on a startup crash, write
    # what happened to a log file and pop up a message box instead of just
    # silently failing to open.
    try:
        main()
    except Exception:
        import os
        import traceback

        log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "crash_log.txt")
        with open(log_path, "w") as f:
            f.write(traceback.format_exc())
        try:
            messagebox.showerror(
                "Arm Controller crashed",
                f"Something went wrong on startup.\n\nDetails were saved to:\n{log_path}",
            )
        except Exception:
            pass  # Tkinter itself may be what's broken; the log file still has the details.
