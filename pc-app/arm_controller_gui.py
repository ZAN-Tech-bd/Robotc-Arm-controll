"""
ZAN Tech Robotic Arm Controller GUI
------------------------------------
A desktop app (Tkinter, built into Python) that talks to the Arduino Nano
arm firmware over USB serial and lets you drag sliders to move every servo.

Works with BOTH arm builds in this repo:
  - firmware/4dof-arm/4dof-arm.ino  (5 servos: Base, Shoulder, Elbow, Wrist, Gripper)
  - firmware/6dof-arm/6dof-arm.ino  (6 servos: Base, Shoulder, Elbow, Wrist Pitch,
                                      Wrist Roll, Gripper)
Pick which one you built from the "Arm Type" dropdown at the top - the slider
panel rebuilds itself to match.

How it works:
  - Pick your arm type, pick your Arduino's serial port, click Connect.
  - Drag any slider - the app sends that one servo's new angle to the arm
    immediately (e.g. "B90\\n"), matching the firmware's serial protocol.
  - "Home All" sends HOMEALL, "Refresh Positions" sends POS and shows the
    arm's reply in the log box at the bottom.

Install the one dependency, then run the app (or just double-click
"Run Arm Controller.bat" - see the README):
    pip install -r requirements.txt
    python arm_controller_gui.py
"""

import os
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

ASSETS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")

# ---------------------------------------------------------------- ZAN Tech brand
# Sampled straight from the official ZAN Tech logo (assets/zantech_logo.png).
ZAN_RED = "#FD110F"
ZAN_BLUE = "#2A2BB7"
ZAN_BLUE_BRIGHT = "#4850E6"  # a lighter blue for things that need to pop on dark bg

BG_WINDOW = "#0A0E1C"
BG_PANEL = "#121833"
BG_PANEL_ALT = "#171F45"
BG_FIELD = "#0E1430"
TEXT_PRIMARY = "#F3F5FC"
TEXT_MUTED = "#8B93B8"
COLOR_OK = "#2ECC71"
COLOR_ERROR = "#FF4D4D"
COLOR_WARN = "#FFB020"

# --------------------------------------------------------------- Arm definitions
# Letter codes must match the single-letter codes each firmware's .ino expects.
ARM_PROFILES = {
    "4dof": {
        "label": "4-DOF Arm  (5 servos)",
        "firmware": "firmware/4dof-arm/4dof-arm.ino",
        "servos": [
            ("Base", "B"),
            ("Shoulder", "S"),
            ("Elbow", "E"),
            ("Wrist", "W"),
            ("Gripper", "G"),
        ],
    },
    "6dof": {
        "label": "6-DOF Arm  (6 servos)",
        "firmware": "firmware/6dof-arm/6dof-arm.ino",
        "servos": [
            ("Base", "B"),
            ("Shoulder", "S"),
            ("Elbow", "E"),
            ("Wrist Pitch", "P"),
            ("Wrist Roll", "R"),
            ("Gripper", "G"),
        ],
    },
}
DEFAULT_PROFILE_KEY = "4dof"


class ArmControllerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ZAN Tech - Robotic Arm Controller")
        self.root.configure(bg=BG_WINDOW)
        self.root.minsize(480, 460)

        self._set_window_icon()
        self._setup_style()

        self.serial_conn = None
        self.read_thread = None
        self.stop_reading = threading.Event()
        self.incoming_queue = queue.Queue()
        self._port_label_to_device = {}
        self._got_any_reply = False

        self.profile_key = DEFAULT_PROFILE_KEY
        self.sliders = {}
        self.value_labels = {}

        self._build_ui()
        self._refresh_ports()
        self._poll_incoming_queue()
        self._fit_window_to_content()

    def _fit_window_to_content(self):
        """Resize the window to snugly fit whatever is currently shown (log
        hidden or visible) instead of leaving dead space or clipping content."""
        self.root.update_idletasks()
        width = max(560, self.root.winfo_reqwidth())
        height = self.root.winfo_reqheight()
        self.root.geometry(f"{width}x{height}")

    # ------------------------------------------------------------- Window chrome
    def _set_window_icon(self):
        icon_path = os.path.join(ASSETS_DIR, "zantech_logo_icon.png")
        try:
            self._icon_image = tk.PhotoImage(file=icon_path)
            self.root.iconphoto(True, self._icon_image)
        except tk.TclError:
            pass  # icon is cosmetic only - fine if the file is missing

    def _setup_style(self):
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass  # fall back to whatever theme is available

        style.configure(".", background=BG_WINDOW, foreground=TEXT_PRIMARY,
                         font=("Segoe UI", 10))
        style.configure("TFrame", background=BG_WINDOW)
        style.configure("Panel.TFrame", background=BG_PANEL)
        style.configure("TLabel", background=BG_WINDOW, foreground=TEXT_PRIMARY)
        style.configure("Panel.TLabel", background=BG_PANEL, foreground=TEXT_PRIMARY)
        style.configure("Heading.TLabel", background=BG_WINDOW, foreground=TEXT_MUTED,
                         font=("Segoe UI", 9, "bold"))
        style.configure("Title.TLabel", background=BG_WINDOW, foreground=TEXT_PRIMARY,
                         font=("Segoe UI", 20, "bold"))
        style.configure("TitleAccent.TLabel", background=BG_WINDOW, foreground=ZAN_RED,
                         font=("Segoe UI", 20, "bold"))
        style.configure("Subtitle.TLabel", background=BG_WINDOW, foreground=TEXT_MUTED,
                         font=("Segoe UI", 9))

        style.configure("TCombobox", fieldbackground=BG_FIELD, background=BG_FIELD,
                         foreground=TEXT_PRIMARY, arrowcolor=TEXT_PRIMARY,
                         bordercolor=BG_FIELD, lightcolor=BG_FIELD, darkcolor=BG_FIELD,
                         padding=6)
        style.map("TCombobox", fieldbackground=[("readonly", BG_FIELD)],
                   foreground=[("readonly", TEXT_PRIMARY)])
        self.root.option_add("*TCombobox*Listbox.background", BG_FIELD)
        self.root.option_add("*TCombobox*Listbox.foreground", TEXT_PRIMARY)
        self.root.option_add("*TCombobox*Listbox.selectBackground", ZAN_BLUE)

        style.configure("Accent.TButton", background=ZAN_RED, foreground="#FFFFFF",
                         font=("Segoe UI", 11, "bold"), padding=(16, 12), borderwidth=0)
        style.map("Accent.TButton", background=[("active", "#D30F0D"), ("disabled", "#5C2A2A")])

        style.configure("Secondary.TButton", background=BG_PANEL_ALT, foreground=TEXT_PRIMARY,
                         font=("Segoe UI", 11), padding=(16, 12), borderwidth=0)
        style.map("Secondary.TButton", background=[("active", "#232C5C"), ("disabled", "#1A2142")])

        style.configure("Connect.TButton", background=ZAN_BLUE, foreground="#FFFFFF",
                         font=("Segoe UI", 10, "bold"), padding=(14, 8), borderwidth=0)
        style.map("Connect.TButton", background=[("active", ZAN_BLUE_BRIGHT), ("disabled", "#2B3066")])

        style.configure("Ghost.TButton", background=BG_PANEL, foreground=TEXT_MUTED,
                         font=("Segoe UI", 9), padding=(8, 6), borderwidth=0)
        style.map("Ghost.TButton", background=[("active", BG_PANEL_ALT)],
                  foreground=[("active", TEXT_PRIMARY)])

        style.configure("Link.TButton", background=BG_WINDOW, foreground=TEXT_MUTED,
                         font=("Segoe UI", 9), padding=(4, 4), borderwidth=0)
        style.map("Link.TButton", foreground=[("active", ZAN_BLUE_BRIGHT)])

        style.configure("Horizontal.TScale", background=BG_PANEL, troughcolor=BG_FIELD,
                         borderwidth=0, sliderlength=18, sliderrelief="flat",
                         troughrelief="flat", lightcolor=BG_FIELD, darkcolor=BG_FIELD)
        style.map("Horizontal.TScale", background=[("active", BG_PANEL)])

    # ------------------------------------------------------------------ UI
    def _build_ui(self):
        root = self.root
        PADX = 20

        # ---------- Header: logo + title + live status, all in one line ----------
        header = ttk.Frame(root)
        header.pack(fill="x", padx=PADX, pady=(20, 18))

        logo_path = os.path.join(ASSETS_DIR, "zantech_logo.png")
        try:
            self._logo_image = tk.PhotoImage(file=logo_path).subsample(2, 2)
            ttk.Label(header, image=self._logo_image, background=BG_WINDOW).pack(side="left", padx=(0, 12))
        except tk.TclError:
            pass  # logo is cosmetic only - app still works without the image file

        title_box = ttk.Frame(header)
        title_box.pack(side="left", fill="x", expand=True)
        title_line = ttk.Frame(title_box)
        title_line.pack(anchor="w")
        ttk.Label(title_line, text="ZAN", style="TitleAccent.TLabel").pack(side="left")
        ttk.Label(title_line, text="TECH", style="Title.TLabel").pack(side="left")
        ttk.Label(title_box, text="Robotic Arm Controller", style="Subtitle.TLabel").pack(anchor="w")

        status_box = ttk.Frame(header)
        status_box.pack(side="right", anchor="e")
        self.status_dot = tk.Canvas(status_box, width=10, height=10, bg=BG_WINDOW,
                                     highlightthickness=0)
        self.status_dot.pack(side="left")
        self._status_dot_id = self.status_dot.create_oval(1, 1, 9, 9, fill=COLOR_ERROR, outline="")
        self.status_label = ttk.Label(status_box, text="Disconnected")
        self.status_label.pack(side="left", padx=(6, 0))

        # ---------- Setup card: arm type + port + connect, one flat panel ----------
        setup_card = tk.Frame(root, bg=BG_PANEL)
        setup_card.pack(fill="x", padx=PADX, pady=(0, 16))
        setup_card.grid_columnconfigure(1, weight=1)

        ttk.Label(setup_card, text="Arm", style="Panel.TLabel", background=BG_PANEL).grid(
            row=0, column=0, padx=(16, 10), pady=(16, 8), sticky="w"
        )
        self.arm_type_combo = ttk.Combobox(
            setup_card, state="readonly",
            values=[profile["label"] for profile in ARM_PROFILES.values()],
        )
        self.arm_type_combo.set(ARM_PROFILES[DEFAULT_PROFILE_KEY]["label"])
        self.arm_type_combo.grid(row=0, column=1, columnspan=3, padx=(0, 16), pady=(16, 8), sticky="ew")
        self.arm_type_combo.bind("<<ComboboxSelected>>", self._on_arm_type_changed)

        ttk.Label(setup_card, text="Port", style="Panel.TLabel", background=BG_PANEL).grid(
            row=1, column=0, padx=(16, 10), pady=(0, 16), sticky="w"
        )
        self.port_combo = ttk.Combobox(setup_card, state="readonly")
        self.port_combo.grid(row=1, column=1, padx=(0, 8), pady=(0, 16), sticky="ew")

        ttk.Button(setup_card, text="⟳", style="Ghost.TButton", width=3,
                   command=self._refresh_ports).grid(row=1, column=2, padx=(0, 8), pady=(0, 16))
        self.connect_btn = ttk.Button(setup_card, text="Connect", style="Connect.TButton",
                                       command=self._toggle_connection)
        self.connect_btn.grid(row=1, column=3, padx=(0, 16), pady=(0, 16))

        # ---------- Servo sliders (rebuilt per arm type) ----------
        ttk.Label(root, text="SERVOS", style="Heading.TLabel").pack(
            anchor="w", padx=PADX + 4, pady=(0, 6)
        )
        self.servo_frame = tk.Frame(root, bg=BG_PANEL)
        self.servo_frame.pack(fill="x", padx=PADX, pady=(0, 16))
        self.servo_frame.grid_columnconfigure(1, weight=1)
        self._build_servo_sliders()

        # ---------- Quick actions ----------
        action_frame = ttk.Frame(root)
        action_frame.pack(fill="x", padx=PADX, pady=(0, 10))
        action_frame.grid_columnconfigure(0, weight=1)
        action_frame.grid_columnconfigure(1, weight=1)
        ttk.Button(action_frame, text="↺  Home All", style="Accent.TButton",
                   command=self._send_home_all).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(action_frame, text="Refresh Positions", style="Secondary.TButton",
                   command=self._send_get_positions).grid(row=0, column=1, sticky="ew", padx=(6, 0))

        # ---------- Collapsible serial console: log + manual command box ----------
        # Hidden by default to keep the main view clean - this is the "advanced" /
        # power-user panel, equivalent to the Arduino IDE's Serial Monitor, for
        # anyone who wants to type raw commands (MENU, T1, HOMEALL, POS, ...)
        # instead of just using the sliders.
        self._log_visible = False
        self.log_toggle_btn = ttk.Button(root, text="▸  Show serial console", style="Link.TButton",
                                          command=self._toggle_log)
        self.log_toggle_btn.pack(anchor="w", padx=PADX + 2, pady=(0, 4))

        self.log_frame = ttk.Frame(root)
        self.log_box = tk.Text(self.log_frame, height=8, state="disabled", wrap="word",
                                bg=BG_FIELD, fg=TEXT_PRIMARY, insertbackground=TEXT_PRIMARY,
                                relief="flat", padx=10, pady=8, font=("Consolas", 9))
        self.log_box.pack(fill="both", expand=True)
        self.log_box.tag_configure("sent", foreground=ZAN_BLUE_BRIGHT)
        self.log_box.tag_configure("recv", foreground=COLOR_OK)
        self.log_box.tag_configure("system", foreground=TEXT_MUTED)

        command_row = tk.Frame(self.log_frame, bg=BG_WINDOW)
        command_row.pack(fill="x", pady=(8, 0))
        self.command_entry = tk.Entry(
            command_row, bg=BG_FIELD, fg=TEXT_PRIMARY, insertbackground=TEXT_PRIMARY,
            relief="flat", font=("Consolas", 10),
        )
        self.command_entry.insert(0, "")
        self.command_entry.pack(side="left", fill="x", expand=True, ipady=6, padx=(0, 8))
        self.command_entry.bind("<Return>", self._send_manual_command)
        ttk.Button(command_row, text="Send", style="Connect.TButton",
                   command=self._send_manual_command).pack(side="left")

        hint = ttk.Label(
            self.log_frame,
            text="Type any firmware command here, e.g. MENU, POS, HOMEALL, T1, EXIT - same as the Arduino Serial Monitor.",
            style="Subtitle.TLabel",
        )
        hint.pack(anchor="w", pady=(6, 0))
        # self.log_frame is intentionally not packed yet - _toggle_log() does that on demand.

        root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _toggle_log(self):
        self._log_visible = not self._log_visible
        if self._log_visible:
            self.log_frame.pack(fill="both", expand=True, padx=20, pady=(0, 16))
            self.log_toggle_btn.config(text="▾  Hide serial console")
        else:
            self.log_frame.pack_forget()
            self.log_toggle_btn.config(text="▸  Show serial console")
        self._fit_window_to_content()

    def _current_profile(self):
        return ARM_PROFILES[self.profile_key]

    def _build_servo_sliders(self):
        for child in self.servo_frame.winfo_children():
            child.destroy()
        self.sliders.clear()
        self.value_labels.clear()

        servos = self._current_profile()["servos"]
        for row, (name, letter) in enumerate(servos):
            top_pad = 16 if row == 0 else 6
            bottom_pad = 16 if row == len(servos) - 1 else 6

            ttk.Label(self.servo_frame, text=name, width=13, background=BG_PANEL).grid(
                row=row, column=0, padx=(16, 8), pady=(top_pad, bottom_pad), sticky="w"
            )

            slider = ttk.Scale(
                self.servo_frame, from_=SLIDER_MIN, to=SLIDER_MAX, orient="horizontal",
                command=lambda val, letter=letter: self._on_slider_move(letter, val),
            )
            # Set the value directly instead of slider.set(), which would fire the
            # command callback above and send a spurious "B90" etc. before Connect.
            slider.configure(value=HOME_ANGLE)
            slider.grid(row=row, column=1, padx=8, pady=(top_pad, bottom_pad), sticky="ew")
            self.sliders[letter] = slider

            value_label = tk.Label(self.servo_frame, text=str(HOME_ANGLE), width=4,
                                    bg=ZAN_BLUE, fg="#FFFFFF", font=("Segoe UI", 9, "bold"))
            value_label.grid(row=row, column=2, padx=(8, 16), pady=(top_pad, bottom_pad))
            self.value_labels[letter] = value_label

    def _on_arm_type_changed(self, _event=None):
        selected_label = self.arm_type_combo.get()
        for key, profile in ARM_PROFILES.items():
            if profile["label"] == selected_label:
                self.profile_key = key
                break
        self._build_servo_sliders()
        self._fit_window_to_content()
        self._log(f"Switched to {self._current_profile()['label']}.", tag="system")

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
            self._log("No serial ports found. Plug in the Arduino and click Refresh.", tag="system")

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

        self.status_dot.itemconfig(self._status_dot_id, fill=COLOR_OK)
        self.status_label.config(text=f"Connected ({port})")
        self.connect_btn.config(text="Disconnect")
        self._log(f"Connected to {port} at {BAUD_RATE} baud.", tag="system")

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
                "example a PC's built-in COM1 instead of the Arduino's USB port), or "
                "the wrong Arm Type for the firmware that's flashed on it.\n\n"
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
        self.status_dot.itemconfig(self._status_dot_id, fill=COLOR_ERROR)
        self.status_label.config(text="Disconnected")
        self.connect_btn.config(text="Connect")
        self._log("Disconnected.", tag="system")

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
                self._log(f"< {text}", tag="recv")
        except queue.Empty:
            pass
        self.root.after(100, self._poll_incoming_queue)

    def _send(self, command):
        if self.serial_conn is None:
            return
        try:
            self.serial_conn.write((command + "\n").encode())
            self._log(f"> {command}", tag="sent")
        except serial.SerialException as exc:
            messagebox.showerror("Send failed", str(exc))
            self._disconnect()

    # ------------------------------------------------------------ Actions
    def _on_slider_move(self, letter, value):
        angle = int(float(value))
        self.value_labels[letter].config(text=str(angle))
        self._send(f"{letter}{angle}")

    def _send_home_all(self):
        self._set_all_sliders(HOME_ANGLE)
        self._send("HOMEALL")

    def _set_all_sliders(self, angle):
        for letter in self.sliders:
            self.sliders[letter].configure(value=angle)
            self.value_labels[letter].config(text=str(angle))

    def _send_get_positions(self):
        self._send("POS")

    def _send_manual_command(self, _event=None):
        """Handles the serial-console text box - lets you type any raw firmware
        command (MENU, T1, EXIT, B45 S90, ...) just like the Arduino Serial
        Monitor, instead of only being able to use the sliders."""
        command = self.command_entry.get().strip()
        if not command:
            return
        if self.serial_conn is None:
            self._log("Not connected - click Connect first.", tag="system")
            return
        self._send(command)
        self._sync_sliders_from_command(command)
        self.command_entry.delete(0, "end")

    def _sync_sliders_from_command(self, command):
        """If the typed command moved a servo this app also has a slider for
        (e.g. "B45"), move that slider to match so the GUI doesn't fall out of
        sync with the arm."""
        upper = command.upper()
        if upper == "HOMEALL":
            self._set_all_sliders(HOME_ANGLE)
            return

        i = 0
        while i < len(upper):
            letter = upper[i]
            if letter in self.sliders:
                j = i + 1
                while j < len(upper) and upper[j].isdigit():
                    j += 1
                if j > i + 1:
                    angle = int(upper[i + 1:j])
                    if 0 <= angle <= SLIDER_MAX:
                        self.sliders[letter].configure(value=angle)
                        self.value_labels[letter].config(text=str(angle))
                    i = j
                    continue
            i += 1

    def _log(self, text, tag=None):
        self.log_box.config(state="normal")
        self.log_box.insert("end", text + "\n", (tag,) if tag else ())
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
