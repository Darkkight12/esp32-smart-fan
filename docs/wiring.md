# Smart Fan Wiring Guide

## Low-voltage connections

| ESP32 connection | Destination |
|---|---|
| GPIO 25 | Speed 1 relay input |
| GPIO 26 | Speed 2 relay input |
| GPIO 27 | Speed 3 relay input |
| GPIO 34 | Potentiometer wiper (middle terminal) |
| 3.3 V | Potentiometer outer terminal |
| GND | Other potentiometer outer terminal |

The potentiometer must provide a signal within the ESP32's permitted ADC input range. Do not apply 5 V to GPIO 34.

## Relay connections

Connect each relay input to its assigned ESP32 GPIO. The current sketch uses active-HIGH relay logic, but verify the module's input behavior and startup state before use.

The relay contacts must be wired according to the specific fan's speed-switching circuit and the relay manufacturer's ratings. Do not assume that the fan's speed wires can safely be connected using a generic diagram.

## Safety

The fan operates from mains electricity. Keep all mains wiring isolated from the ESP32 and potentiometer circuit. Use a suitable insulated enclosure and correctly rated components. Have the mains side inspected by a qualified electrician.

Do not connect the fan to the relay module until low-voltage control has been tested with mains power disconnected.
