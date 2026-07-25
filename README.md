# JARVIS Smart Board

A fully offline, WiFi-based smart home relay controller built on an ESP32 — no cloud service, no internet connection, and no third-party app required. The board creates its own local WiFi network and hosts a custom HUD-style dashboard directly from flash memory.

Built and maintained by **Shivans**.

---

## Features

- **Fully offline** — the ESP32 runs its own WiFi Access Point. Everything works with zero internet access.
- **6-channel relay control** — toggle each device individually, with editable names and live status.
- **Custom JARVIS-style dashboard** — dark glassmorphism UI, animated HUD elements, and a central "arc reactor" master power control.
- **Automation scenes** — Night, Movie, Gaming, Sleep, Vacation, and Emergency Shutdown presets.
- **Live system diagnostics** — free heap, uptime, CPU temperature, connected client count, and more, polled every second.
- **Session-based login** — basic access control suitable for a private home network.
- **Persistent state** — relay states, device names, and WiFi credentials survive a reboot (stored in flash/NVS).
- **Safe by design** — relays default to fail-safe OFF, and the wiring uses NO (Normally Open) terminals throughout.

## Wiring Diagram

![JARVIS Smart Board Wiring Diagram](docs/wiring-diagram.svg)

| ESP32 Pin | Relay Module Pin | Channel | Default Device |
|---|---|---|---|
| GPIO 23 | IN1 | Relay 1 | Room Light |
| GPIO 22 | IN2 | Relay 2 | Ceiling Fan |
| GPIO 21 | IN3 | Relay 3 | Television |
| GPIO 19 | IN4 | Relay 4 | Power Socket |
| GPIO 18 | IN5 | Relay 5 | Spare / customizable |
| GPIO 5  | IN6 | Relay 6 | Spare / customizable |
| 5V | VCC | — | Logic + coil power |
| GND | GND | — | Common ground |

> ⚠️ **AC mains warning:** This project switches mains-voltage appliances. Power off the circuit before wiring the AC side, verify with a tester, and consult a licensed electrician if you're not confident working with mains wiring.

## Hardware Required

- ESP32 DevKit V1 (or any standard ESP32 dev board)
- 6-Channel relay module (Active LOW, opto-isolated recommended)
- Regulated 5V / 2A USB power supply
- AC appliances/wiring as needed

## Getting Started

1. Install the **esp32 by Espressif Systems** board package in Arduino IDE (Boards Manager) if you haven't already.
2. Open `JARVIS_SmartBoard.ino` in Arduino IDE.
3. Select **Tools → Board → ESP32 Dev Module**, and the correct **Port**.
4. Wire the hardware according to the diagram above — logic side first, verify with a multimeter before connecting mains.
5. Click **Verify**, then **Upload**.
6. Open **Serial Monitor** at `115200` baud to confirm the board booted and see its IP.
7. Connect to WiFi **`JARVIS_BOARD`** (default password: `IronMan@2026`) from your phone or laptop.
8. Open `http://192.168.4.1` in a browser.
9. Log in with the default credentials (**admin / jarvis123**) and start controlling your devices.

> 🔐 **Security note:** The default login and WiFi credentials are meant for a private home network only. If you're deploying this somewhere less trusted, change both in the source before flashing.

## Project Structure

```
jarvis-smart-board/
├── JARVIS_SmartBoard.ino     ← Arduino sketch (open this in Arduino IDE)
└── docs/
    └── wiring-diagram.svg     ← wiring diagram used above
```

## Tech Stack

- **Firmware:** C++ (Arduino core for ESP32), `WiFi.h`, `WebServer.h`, `Preferences.h`
- **Frontend:** Vanilla HTML/CSS/JavaScript, served entirely from ESP32 flash — no external dependencies, no build step
- **Networking:** ESP32 SoftAP + REST-style JSON API, polled via `fetch()`

## Roadmap

- [ ] MQTT integration for Home Assistant / Node-RED
- [ ] OTA firmware updates
- [ ] Real ambient temperature sensor (DS18B20 / DHT22)
- [ ] Real power/current metering per channel
- [ ] Companion mobile app (Android/iOS)



---

*JARVIS Smart Board — by Shivans*
