# ZAN Tech Arm Controller — Web Edition

A single HTML file that controls the robotic arm straight from your browser
over USB, using the [Web Serial API](https://developer.chrome.com/docs/capabilities/serial) -
**no Python, no install, nothing to run**. This is for anyone who wants the
[PC app](../pc-app/)'s experience (pick your arm, drag sliders) but doesn't
have Python set up, or just wants to open a web page and go.

![Main screen](../docs/screenshots/web-app-main.jpg)

## Use it

**Online:** open the hosted version (see the main [repo README](../README.md#10-web-app-no-install-needed)
for the link, once GitHub Pages is enabled on this repo).

**Locally, no server needed:** download [`index.html`](index.html) (and the
`assets/` folder next to it) and just double-click it to open in **Chrome**
or **Edge** - Web Serial works fine from a local `file://` page, no server
required.

Then:

1. Pick your **Arm Type** (4-Servo / 4-DOF / 6-DOF).
2. Plug the Arduino Nano or ESP32 in over USB.
3. Click **Connect over USB**, and pick your board from the browser's device
   picker (this is the browser's own native dialog, not part of this page).
4. Drag the sliders. **Home All** / **Refresh Positions** work the same as
   the PC app. **Show serial console** reveals a log + command box for typing
   raw firmware commands (`MENU`, `POS`, `T1`, `B45 E120`, ...).

## Requirements and limits

- **Browser**: a Chromium-based desktop browser - **Chrome** or **Edge**.
  Web Serial is not available in Firefox, Safari, or any mobile browser
  (use the [mobile app](../mobile-app/) on Android instead, which uses
  Bluetooth rather than USB).
- **Connection**: this talks over **USB** (serial), the same as the Arduino
  IDE's Serial Monitor or the PC app. It works with *any* of the firmware
  variants' USB port, including the `-hc05` and `-esp32` ones - Web Serial
  itself doesn't do Bluetooth (browsers don't expose classic Bluetooth SPP to
  web pages), only the USB cable.
- No build step, no dependencies, no bundler - it's one static HTML file
  with inline CSS/JS.

## Why no Bluetooth here

Browsers' Web Bluetooth API only supports **Bluetooth Low Energy (BLE)**, not
the **classic Bluetooth (RFCOMM/SPP)** that HC-05 modules and the ESP32
firmware's `BluetoothSerial` use. For Bluetooth control, use the
[mobile app](../mobile-app/) (Android) instead - it talks classic Bluetooth
natively. This web page is the USB/no-install option.

## Hosting your own copy

It's fully static - any web host works (GitHub Pages, Netlify, a folder on
your own server, or just the local file). To enable GitHub Pages for this
repo: **Settings → Pages → Deploy from a branch**, then point it at this
`web-app/` folder (or copy `index.html` + `assets/` to a `docs/` folder /
`gh-pages` branch, whichever your Pages setup expects).
