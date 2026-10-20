# ESP32 Smart Fan

An IoT-based smart fan controller built using an ESP32, relay module, and potentiometer. The project combines physical speed selection, a local web dashboard, timer-based shutdown, and smart-home control through Sinric Pro.

## Features

- **Three-speed control:** Select OFF, Speed 1, Speed 2, or Speed 3 using relay outputs.
- **Potentiometer control:** Adjust the fan through a physical potentiometer connected to the ESP32's ADC input.
- **Web dashboard:** Control the fan from a browser on the local network.
- **PIN-protected access:** Web interface authentication with session handling and login lockout.
- **Automatic shutdown timer:** Set a timer to switch the fan off automatically.
- **Wi-Fi configuration:** Configure network connectivity through the WiFiManager setup portal.
- **Smart-home integration:** Sinric Pro integration for supported voice-assistant control.
- **Local network access:** mDNS hostname `smartfan.local` where supported.
- **Wi-Fi watchdog:** Restart the ESP32 if Wi-Fi remains disconnected for the configured timeout.
- **Offline physical control:** The potentiometer control loop runs independently of the web dashboard.
## Hardware

### Main Controller

- ESP32 development board with CP2102 USB-to-serial interface
- Three-channel relay module
- Approximately 49–50 kΩ potentiometer
- Three-speed pedestal fan

### Optional Subassembly 1: Logic Shifter

The three-channel transistor logic shifter can be built from individual components or replaced by a compatible ready-made module.

**Individual components:**
- 3 × 2N2222A/PN2222A NPN transistors
- 6 × 10 kΩ resistors (3 base resistors and 3 pull-up resistors)

**Purchase links:**
- [Robu — 2N2222A transistors](https://robu.in/?s=2N2222A&post_type=product)
- [Robocraze — 2N2222A transistors](https://robocraze.com/search?q=2N2222A)
- [Robocraze — 4-channel logic-level converter](https://robocraze.com/products/smartelex-4p-llc-bi-directional-logic-level-converter)

The transistor circuit inverts its input signal. Verify compatibility before substituting a different module.

### Optional Subassembly 2: Startup-Delay Circuit

The startup-delay circuit can be assembled from individual components or laid out on a separate PCB.

**Individual components:**
- 1 × PN2222A NPN transistor
- 1 × 2N2907A PNP transistor
- 3 × 10 kΩ resistors
- 1 × 2.2 kΩ resistor
- 2 × 220 µF electrolytic capacitors

**Purchase links:**
- [Robu — PN2222A](https://robu.in/?s=PN2222A&post_type=product)
- [Robocraze — PN2222A](https://robocraze.com/search?q=PN2222A)
- [Robu — 2N2907A](https://robu.in/?s=2N2907A&post_type=product)
- [Robocraze — 2N2907A](https://robocraze.com/search?q=2N2907A)
- [Robu — electrolytic capacitors](https://robu.in/product-category/aluminium-electrolytic-capacitors/)
- [Robocraze — 220 µF capacitors](https://robocraze.com/search?q=220uF+electrolytic+capacitor)

### PCB Options

Build each subassembly on a prototyping board, or design a custom PCB.

- [Robu — Custom PCB manufacturing](https://robu.in/product/online-pcb-manufacturing-service/)
- [Robocraze — Prototyping boards](https://robocraze.com/collections/prototyping-boards)

A bare PCB does not include components unless assembly is explicitly offered. Prepare and verify the circuit layout before ordering.

See [`docs/wiring.md`](docs/wiring.md) for the complete pin mapping, wiring tables, and safety notes.

## Pin Configuration

| Component | ESP32 pin |
|---|---|
| Speed 1 relay | GPIO 25 |
| Speed 2 relay | GPIO 26 |
| Speed 3 relay | GPIO 27 |
| Potentiometer signal | GPIO 34 |

The current sketch defines the relay outputs as active-HIGH:

- `RELAY_ON = HIGH`
- `RELAY_OFF = LOW`

Confirm that these levels match your relay module before connecting the fan.

## How It Works

The ESP32 reads the potentiometer and maps its ADC reading to one of the fan's operating states. When a speed change is requested, the controller switches the relay outputs with a short transition delay.

The web server provides a dashboard for changing fan speed and configuring the shutdown timer. Sinric Pro handlers process supported smart-home commands and report state changes.

Wi-Fi configuration is handled by WiFiManager. The sketch also includes a watchdog that restarts the ESP32 after an extended Wi-Fi disconnection.

## Potentiometer Mapping

The current code divides the 12-bit ADC range into four zones:

| ADC reading | Fan state |
|---|---|
| 0–1023 | OFF |
| 1024–2047 | Speed 1 |
| 2048–3071 | Speed 2 |
| 3072–4095 | Speed 3 |

The sketch uses hysteresis to help prevent unwanted changes near zone boundaries.

## Software Requirements

Install the following libraries through the Arduino IDE Library Manager or their official distribution sources:

- **SinricPro** — smart-home integration
- **ArduinoJson** — JSON handling
- **WebSockets** by Markus Sattler — required by the Sinric Pro stack
- **WiFiManager** by tzapu — Wi-Fi configuration portal

The sketch also uses ESP32 Arduino framework components, including `WiFi`, `WebServer`, `ESPmDNS`, and `Preferences`.

Use a compatible ESP32 Arduino core and the library versions required by the current sketch. Verify compilation with your installed versions before publishing a release.

## Getting Started

### 1. Clone the repository

```bash
git clone https://github.com/YOUR_USERNAME/esp32-smart-fan.git
cd esp32-smart-fan
```

Replace `YOUR_USERNAME` with your GitHub username.

### 2. Open the sketch

Open `src/SmartFan.ino` in the Arduino IDE and select the correct ESP32 board and serial port.

### 3. Install the dependencies

Install the required libraries listed above. Select an ESP32 board package compatible with the sketch.

### 4. Configure smart-home integration

If you want to use Sinric Pro, create a device in Sinric Pro and enter the required credentials in the configuration section of the sketch.

**Do not publish your actual API keys, Wi-Fi passwords, or other secrets in a public repository.** Use placeholders in the public code and enter private values locally.

### 5. Upload and configure Wi-Fi

Upload the sketch and open the Serial Monitor at the baud rate configured in the code. If Wi-Fi configuration is required, connect to the `SmartFan-Setup` portal and follow its configuration flow.

### 6. Access the dashboard

Once connected to Wi-Fi and the web services have started, open the local dashboard using the address shown by the sketch. The hostname `smartfan.local` may work on networks that support mDNS.

### 7. Test before connecting the fan

First test the controller with the fan disconnected. Verify relay states, potentiometer zones, web controls, timer expiry, and recovery after Wi-Fi loss.

## Security Notes

- Change the default web PIN before using the controller.
- Do not commit Wi-Fi credentials or Sinric Pro secrets.
- Use a unique PIN rather than the example/default value.
- Keep the controller on a trusted network and review the web interface's security limitations before exposing it to other networks.

## Electrical Safety

This project is intended for educational and personal IoT experimentation. A mains-powered fan can cause electric shock, fire, or injury.

- Disconnect power before wiring or modifying the fan.
- Use relays rated for the fan's voltage, current, and motor-load characteristics.
- Ensure proper insulation, strain relief, enclosure protection, and separation between mains and low-voltage circuitry.
- Never connect mains voltage to ESP32 GPIO pins.
- Have mains wiring checked by a qualified person.
- Do not leave an untested prototype operating unattended.

A software command to turn the relays off is not a substitute for safe hardware design.

## Current Limitations

- Wi-Fi-dependent services require a successful network connection.
- `smartfan.local` depends on mDNS support on the client network.
- Relay behavior and fan speed wiring depend on the particular relay module and fan.
- Smart-home functionality requires valid Sinric Pro configuration and network access.
- The Wi-Fi watchdog restarts the controller after a prolonged disconnection; it is not a guarantee of uninterrupted network service.


## License

This project is published under the MIT License. See `LICENSE` for details.

## Author

Created as an ESP32 and IoT smart-home project. Contributions, suggestions, and issue reports are welcome.
