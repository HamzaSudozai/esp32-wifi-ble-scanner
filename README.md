# ESP32 Wi-Fi + BLE Environment Scanner

A passive wireless survey tool that runs entirely on an ESP32. It scans nearby Wi-Fi networks and BLE devices and serves a live dashboard from the board itself, so you can see what's actually being broadcast around you.

<!-- Add a screenshot after redacting other people's SSIDs/MACs: -->
<!-- ![Dashboard](docs/dashboard.png) -->

## Features

- **Wi-Fi scan:** SSID, BSSID, channel, RSSI, encryption type (hidden SSIDs included)
- **BLE scan:** device name, MAC, RSSI, advertised service UUID, Bluetooth SIG company ID
- **Security highlighting:** Open and WEP networks flagged in red, legacy WPA in amber
- **Channel congestion:** busiest channel and network count
- **Live web dashboard:** served by the ESP32, auto-refreshing, with BLE filtering
- **Dual-core design:** scanning runs in a background FreeRTOS task on core 0 so the web server on core 1 stays responsive

## Hardware

- ESP32 dev board (tested on an ESP32-WROOM-32U with an external u.FL antenna)
- Micro-USB data cable

## Setup

1. Install **Arduino IDE 2.x** and the **esp32 by Espressif Systems** board package (tested on 3.3.12).
2. Copy `esp32_scanner/secrets.h.example` to `esp32_scanner/secrets.h` and enter your 2.4 GHz Wi-Fi name and password.
3. Open `esp32_scanner/esp32_scanner.ino`.
4. Select **Tools > Board > ESP32 Dev Module** and **Tools > Partition Scheme > Huge APP (3MB No OTA/1MB SPIFFS)**. Wi-Fi plus BLE does not fit in the default partition.
5. Upload (hold the **BOOT** button while it shows `Connecting...`).
6. Open the Serial Monitor at **115200 baud**, press **EN**, and browse to the printed IP address.

If the board can't join your Wi-Fi, it starts a fallback hotspot (`ESP32-Scanner`) and serves the dashboard at `192.168.4.1`.

## What I learned

- 802.11 scan results expose a lot: BSSIDs, channels, and encryption modes are all visible without authenticating. A router advertising mixed WPA/WPA2 is easy to spot.
- BLE advertisements leak device names and vendor company IDs (e.g. `0x004C` is Apple) even when unnamed, and random MAC addresses limit tracking.
- SSIDs and BLE names are attacker-controlled input. The dashboard escapes every value before rendering to prevent XSS from a malicious network name.
- Arduino's prototype generator can mistake JavaScript inside a raw string for C++ functions. Using arrow functions avoids it.

## Ideas / roadmap

- [ ] Evil-twin detection (same SSID with different BSSID or encryption)
- [ ] Deauth-flood detection in promiscuous mode
- [ ] Log results to SD card or a remote server
- [ ] OLED display for standalone use

## Ethics and legal

This tool is **passive**: it only listens to broadcast beacons and advertisements and does not attack, deauthenticate, or connect to other networks or devices. Only use it where you are permitted to, and never publish other people's network or device identifiers.

## License

MIT
