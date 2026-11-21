# ispu-node: air quality monitor (ESP32)

An ESP32 reads PM2.5, gas, temperature, and humidity, sets an air quality class, and switches LEDs, a buzzer, and a servo vent. A small model on the chip forecasts the class three hours ahead, but the forecast is advice only.

## Running the simulation

Press the green Start button, then wait for `Air quality monitor ready` in the serial monitor. The firmware prints one line every 2 seconds:

```text
AIR=BAIK FORECAST3H=BAIK pm25=6.0 gas=0 gas_adc=0 trend=0.00 temp=30.0 hum=60.0
```

`AIR` is the current class, and `FORECAST3H` is the model forecast.

## Changing the air

Click a part to change what it reads.

| Part | Reads | Range |
|:---|:---|:---|
| Potentiometer `pm` (pin 34) | PM2.5 | 0 to 300 µg/m³ |
| Potentiometer `gas` (pin 35) | Gas level | 0 to 1000 ppm |
| DHT22 (pin 15) | Temperature and humidity | Set on its sliders |

To raise the alarm, drag the `pm` potentiometer to 85 percent of its travel. That reads 255 µg/m³, and the class becomes `BERBAHAYA`.

## Reading the outputs

The current class is the worse of the PM2.5 class and the gas class, and it drops one step only after three lower readings in a row.

| Class | PM2.5 limit (µg/m³) | Gas limit (ppm) | LEDs | Servo | Buzzer (pin 4) |
|:---|:---|:---|:---|:---|:---|
| `BAIK` | up to 15.5 | up to 200 | Green | 0° | Off |
| `SEDANG` | up to 55.4 | up to 400 | Yellow | 30° | Off |
| `TIDAK_SEHAT` | up to 150.4 | up to 800 | Yellow and red | 60° | Short beeps at 1000 Hz |
| `BERBAHAYA` | above 150.4 | above 800 | Red | 90° | Steady tone at 2000 Hz |

The LEDs sit on pins 25 (green), 26 (yellow), and 27 (red), and the servo is on pin 13.

## Knowing the limits

- Potentiometers stand in for the PM2.5 and gas sensors, so no real sensor was tested.
- One demo hour is 6 seconds, and the forecast needs about 18 seconds of readings before it settles.
- The simulation sends readings to a public MQTT broker, so do not rely on it for anything private.
