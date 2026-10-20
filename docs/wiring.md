# Smart Fan Wiring Guide

This guide documents the ESP32 smart fan controller, the 2N2222/PN2222A transistor logic shifters, and the experimental hardware startup-delay circuit.

## 1. Main Components

- ESP32 development board
- Three-channel 5 V relay module
- Three 2N2222A/PN2222A NPN transistors for the relay logic-shifter channels
- One additional PN2222A transistor for the shared startup-delay circuit
- 10 kΩ resistors
- Two 220 µF electrolytic capacitors
- 2.2 kΩ resistor, where used in the existing circuit
- Approximately 49 kΩ potentiometer
- Three-speed pedestal fan

Verify the actual component count and resistor placement against your assembled circuit before reproducing it.

## 2. ESP32 Pin Mapping

| Function | ESP32 pin |
|---|---|
| Speed 1 control | GPIO 25 |
| Speed 2 control | GPIO 26 |
| Speed 3 control | GPIO 27 |
| Potentiometer signal | GPIO 34 |
| Logic reference | GND |

The firmware uses active-HIGH relay commands. However, the NPN logic-shifter stage described below inverts the signal, so the voltage at the transistor collector is the opposite of the ESP32 GPIO logic level.

## 3. Potentiometer Wiring

| Potentiometer terminal | Connection |
|---|---|
| Outer terminal 1 | ESP32 3.3 V |
| Middle terminal (wiper) | GPIO 34 |
| Outer terminal 2 | ESP32 GND |

The potentiometer signal must remain within the ESP32's permitted input voltage range. Never connect 5 V to GPIO 34.

## 4. 2N2222A Relay Logic Shifter

Build one transistor stage for each relay control channel.

### Connections for each channel

| Component terminal | Connection |
|---|---|
| ESP32 GPIO | Through a 10 kΩ resistor to the transistor base |
| Transistor emitter | GND |
| Transistor collector | Corresponding relay-control output |
| 10 kΩ pull-up resistor | Between +5 V and the transistor collector |
| Relay-control output | Corresponding relay input, according to the existing board wiring |

Use a common ground between the ESP32 and the 5 V relay-control circuit.

### How it works

The NPN transistor operates as an inverting switch:

- GPIO HIGH: transistor conducts and pulls the collector output LOW.
- GPIO LOW: transistor turns off and the pull-up resistor raises the collector output toward +5 V.

**Important:** This stage inverts the logic. Because the current firmware defines the relay commands as active-HIGH, verify the relay module's input polarity and the complete signal path before connecting the fan. The transistor circuit must not be assumed to preserve the firmware's logic polarity.

### Transistor pinout

Do not assume that every 2N2222, PN2222A, or compatible transistor has the same E-B-C pin order. Check the exact manufacturer's datasheet and identify the emitter, base, and collector before soldering.

## 5. Hardware Startup-Delay Circuit

The purpose of the additional transistor circuit is to keep the relay logic-shifter stages disabled during the initial ESP32 boot period, reducing the chance of unintended relay activation.

### Components used in the attempted circuit

- One additional PN2222A NPN transistor
- Two 220 µF electrolytic capacitors
- 10 kΩ resistors
- A 3.3 V reference and common ground

### Intended connections

The previously discussed shared-delay arrangement uses the additional transistor as a temporary clamp on the three logic-shifter transistor base nodes.

| Delay-circuit connection | Intended destination |
|---|---|
| Additional transistor emitter | Common GND |
| Additional transistor collector | The three logic-shifter base nodes |
| Additional transistor base | The RC timing network |
| RC network | 3.3 V reference and GND, using the installed capacitors and resistors |

The two 220 µF capacitors and the 10 kΩ resistors form the timing network. The clamp transistor is intended to hold the logic-shifter stages in a defined state during startup, then release them after the timing interval.

**Do not use this connection table alone to construct the circuit.** The exact capacitor polarity, resistor placement, and transistor-base connection must match a verified schematic. A capacitor connected in the wrong position can produce a brief pulse rather than a sustained startup delay.

### Current limitation

The delay circuit has been tested, but delaying the 5 V supply to the logic-shifter board did not prevent the relays from activating during startup. Therefore, the delay arrangement should be considered **unverified**, not a confirmed fix.

Test the control circuit with the fan disconnected and monitor all three relay inputs during power-up, reset, and power-down. Confirm that no two speed relays can be active simultaneously.

## 6. Relay Module and Fan

The three relay channels select the fan's discrete speed inputs. The firmware uses GPIO 25, GPIO 26, and GPIO 27 for the three speed commands and includes a 250 ms break-before-make transition delay.

Before connecting the fan:

1. Test the ESP32 and logic shifters with the mains supply disconnected.
2. Confirm the active state of each relay input.
3. Confirm that the three speed channels cannot become active together during startup or reset.
4. Verify the relay contact ratings for the fan's motor load.
5. Check insulation, clearances, enclosure protection, and wiring with a qualified person.

## 7. Electrical Safety

This project controls a mains-powered fan.

- Disconnect mains power before modifying any wiring.
- Keep mains wiring physically separated from the ESP32 and low-voltage circuit.
- Never connect mains voltage to an ESP32 pin or transistor logic input.
- Use correctly rated relays, insulated terminals, strain relief, and a suitable enclosure.
- Do not rely on software delays or an unverified RC circuit as the sole protection against unintended motor operation.
- Have the mains wiring inspected by a qualified electrician.
