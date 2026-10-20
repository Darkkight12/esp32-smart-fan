## 1. Components and Build Options

The project is divided into three hardware groups:

1. Main smart fan controller
2. Logic-shifter circuit
3. Startup-delay circuit

The logic-shifter and startup-delay circuits can each be built from individual components or assembled on a PCB. They are separate from the main ESP32, potentiometer, and fan hardware.

### A. Main Smart Fan Controller

| Component | Quantity | Purpose |
|---|---:|---|
| ESP32 development board with CP2102 | 1 | Main controller |
| Three-channel relay module | 1 | Selects fan speed |
| Potentiometer, approximately 49–50 kΩ | 1 | Physical speed control |
| Three-speed pedestal fan | 1 | Controlled appliance |
| Suitable low-voltage power supply | As required | Powers the controller and relay module |

### B. Logic-Shifter Circuit

**Build from individual components**

| Component | Quantity |
|---|---:|
| 2N2222A/PN2222A NPN transistor | 3 |
| 10 kΩ base resistor | 3 |
| 10 kΩ collector pull-up resistor | 3 |
| Suitable prototyping PCB or custom PCB | Optional |

Each channel uses one NPN transistor, one base resistor, and one collector pull-up resistor. Three channels are used for the three ESP32 speed-control outputs.

**Purchase alternative**

- [Robu — Search for 2N2222A transistors](https://robu.in/?s=2N2222A&post_type=product)
- [Robocraze — Search for 2N2222A transistors](https://robocraze.com/search?q=2N2222A)
- [Robu — Search for 10 kΩ resistors](https://robu.in/?s=10k+resistor&post_type=product)
- [Robocraze — Search for 10 kΩ resistors](https://robocraze.com/search?q=10k+resistor)
- [Robocraze — 4-channel logic-level converter](https://robocraze.com/products/smartelex-4p-llc-bi-directional-logic-level-converter)

The transistor circuit is an inverting stage. A ready-made bidirectional logic-level converter is an alternative only if its behavior is compatible with the relay inputs; it is not automatically an exact replacement.

### C. Startup-Delay Circuit

**Build from individual components**

| Component | Quantity |
|---|---:|
| PN2222A NPN transistor | 1 |
| 2N2907A PNP transistor | 1 |
| 10 kΩ resistors | 3 |
| 2.2 kΩ resistor | 1 |
| 220 µF electrolytic capacitors | 2 |
| Suitable prototyping PCB or custom PCB | Optional |

This circuit is intended to delay the relay supply during startup.

**Purchase alternative**

- [Robu — Search for PN2222A transistors](https://robu.in/?s=PN2222A&post_type=product)
- [Robocraze — Search for PN2222A transistors](https://robocraze.com/search?q=PN2222A)
- [Robu — Search for 2N2907A transistors](https://robu.in/?s=2N2907A&post_type=product)
- [Robocraze — Search for 2N2907A transistors](https://robocraze.com/search?q=2N2907A)
- [Robu — Browse electrolytic capacitors](https://robu.in/product-category/aluminium-electrolytic-capacitors/)
- [Robocraze — Search for 220 µF capacitors](https://robocraze.com/search?q=220uF+electrolytic+capacitor)
- [Robu — Search for a 2.2 kΩ resistor](https://robu.in/?s=2.2k+resistor&post_type=product)
- [Robocraze — Search for a 2.2 kΩ resistor](https://robocraze.com/search?q=2.2k+resistor)

For the capacitors, select the appropriate voltage rating and observe polarity. Verify the transistor pinouts against the exact parts you purchase.

### D. PCB Options

You can use a separate PCB for each circuit or combine both circuits onto a custom board.

- [Robu — Custom PCB manufacturing](https://robu.in/product/online-pcb-manufacturing-service/)
- [Robocraze — Prototyping boards](https://robocraze.com/collections/prototyping-boards)

A bare PCB does not include the components unless assembly is explicitly included in the order. For a custom PCB, prepare and verify the schematic and layout before generating Gerber files.

## 2. ESP32 Pin Mapping

| Function | ESP32 pin |
|---|---|
| Speed 1 control | GPIO 25 |
| Speed 2 control | GPIO 26 |
| Speed 3 control | GPIO 27 |
| Potentiometer signal | GPIO 34 |

## 3. Logic-Shifter Circuit

Build three identical NPN transistor stages.

```text
                 +5 V
                   |
                 10 kΩ
                   |
                   +------ OUT → compatible 5 V logic input
                   |
                Collector
                   |
ESP32 GPIO--10 kΩ--Base
                   |\
                   | >  2N2222A
                   |/
                Emitter
                   |
                  GND
```

- GPIO LOW: transistor off; output pulled HIGH toward 5 V.
- GPIO HIGH: transistor on; output pulled LOW.

This circuit inverts the signal. Confirm the relay module's input polarity and compatibility before connecting it.

## 4. Startup-Delay Circuit

Use the PN2222A, 2N2907A, three 10 kΩ resistors, one 2.2 kΩ resistor, and two 220 µF electrolytic capacitors as specified in the circuit.

| Component pin | Connection |
|---|---|
| 2N2907A pin 1 (E) | +5 V |
| 2N2907A pin 2 (B) | PN2222A pin 3 (C), plus 2.2 kΩ resistor to +5 V |
| 2N2907A pin 3 (C) | Relay VCC |
| PN2222A pin 1 (E) | GND |
| PN2222A pin 2 (B) | Three 10 kΩ resistors and two 220 µF capacitors, according to the assembled circuit |
| PN2222A pin 3 (C) | 2N2907A pin 2 (B) |
| Relay GND | Common GND |
| Both capacitor positive terminals | PN2222A base |
| Both capacitor negative terminals | GND |

Preserve the actual resistor-chain connections when building the circuit or laying out a PCB. Verify the exact transistor pinout and capacitor polarity before applying power.

## 5. Safety

- Disconnect mains power before modifying wiring.
- Keep mains AC wiring isolated from the ESP32 and low-voltage circuitry.
- Never connect mains voltage to ESP32 GPIO pins.
- Use suitably rated relays, insulation, and enclosures.
- Have mains wiring inspected by a qualified electrician.
