@echo off
rem ============================================================
rem  Double-click this file to open the Robotic Arm Controller.
rem  No need to open a terminal or type any Python commands.
rem ============================================================

cd /d "%~dp0"

rem --- Make sure Python is installed ---
where python >nul 2>nul
if errorlevel 1 (
    echo.
    echo [ERROR] Python is not installed, or not added to PATH.
    echo         Install it from https://www.python.org/downloads/
    echo         and tick "Add Python to PATH" during setup.
    echo.
    pause
    exit /b 1
)

rem --- Make sure Tkinter (the GUI toolkit) is available ---
python -c "import tkinter" >nul 2>nul
if errorlevel 1 (
    echo.
    echo [ERROR] Your Python install is missing Tkinter ^(the GUI toolkit^).
    echo         Reinstall Python from https://www.python.org/downloads/
    echo         with the default options - Tkinter is included automatically.
    echo.
    pause
    exit /b 1
)

rem --- Make sure the pyserial library is installed (first run only) ---
python -c "import serial" >nul 2>nul
if errorlevel 1 (
    echo Installing the one required package ^(pyserial^)...
    python -m pip install -r requirements.txt
    if errorlevel 1 (
        echo.
        echo [ERROR] Could not install pyserial. Check your internet connection.
        echo.
        pause
        exit /b 1
    )
)

rem --- Launch the GUI without a console window popping up.
rem     If it crashes on startup, arm_controller_gui.py writes the error to
rem     crash_log.txt itself (pythonw hides console output), so check that
rem     file if double-clicking this doesn't open a window. ---
start "" pythonw "arm_controller_gui.py"
