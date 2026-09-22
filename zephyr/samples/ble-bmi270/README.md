# Spotflow BLE BMI270 sample

This sample sends BMI270 accelerometer and gyroscope readings through the
Spotflow BLE log transport once per second. It targets `lp_em_cc2340r5`.

## Wiring

| LaskaKit BMI270 | LP-EM-CC2340R5 |
| --- | --- |
| VCC | BoosterPack pin 1 (3.3 V) |
| GND | BoosterPack pin 20 or 22 |
| SCL | BoosterPack pin 9 (DIO24) |
| SDA | BoosterPack pin 10 (DIO0) |

The overlay uses the module's default I2C address, `0x68`. The LaskaKit module
already includes 4.7 kohm pull-up resistors on SDA and SCL. Interrupt pins are
not used.

When using an LP-XDS110ET, install its `P9` (`TGT VDD`) jumper across pins 1-2,
marked `XDS110`, to power the target from the debugger. Pins 2-3 select an
external target supply instead. The separate 10-pin ARM debug connector does
not carry target power.

Before connecting the sensor, verify approximately 3.3 V between BoosterPack
pin 1 and pin 20 or 22. Do not drive the target from the debugger and an
external supply at the same time.

## Build

From the Zephyr module root, activate the workspace virtual environment and run:

```powershell
west build -b lp_em_cc2340r5 -p always samples/ble-bmi270
west flash
```

Building from the module root keeps the build path short enough for the TI HAL
on Windows.

Each log reports acceleration in mm/s^2 and angular velocity in mrad/s:

```text
BMI270 a_mm_s2=[12,-34,9806] g_mrad_s=[0,1,-2]
```

The sample uses direct single-transaction I2C register writes. This works around
an incompatibility between the BMI270 driver and the CC23xx I2C controller in
the TI Zephyr downstream, where split register writes can succeed without
actually updating the sensor registers.
