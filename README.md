# ispu-node

Simulate an ESP32 air quality monitor that classifies PM2.5 now, forecasts its class three hours ahead with a model on the chip, and shows both on a Node-RED dashboard.

The project is the final assignment of a digital literacy course at Universitas Negeri Surabaya, and it supports SDG 3 (health), SDG 11 (cities), and SDG 13 (climate action). You can run all of it without hardware, and you can also try the circuit in your browser in the [Wokwi project](https://wokwi.com/projects/481313548617739265).

## Understanding the goal

Official air quality stations are few, so many schools and neighborhoods have no local data. A small sensor node can fill that gap, if it turns raw numbers into a clear class and warns people in time.

This project builds that node in simulation, and the ESP32 reads temperature, humidity, PM2.5, and a gas level every 2 seconds. It sets the current class from the Indonesian ISPU limits, then switches LEDs, a buzzer, and a servo vent to match.

A small random forest on the chip forecasts the class three hours ahead, but the forecast is advice only. It beats the guess "same class as now" by only 0.6 points (0.741 against 0.735 accuracy), so the alarm never follows it.

## Running the simulation

Wokwi runs the simulation in the cloud, so you need a free Wokwi account. The free plan allows 50 minutes of simulation in a rolling 30 days, and each session ends after 5 minutes.

You need these tools: `just`, [PlatformIO](https://platformio.org/) (`pio`), [uv](https://docs.astral.sh/uv/), and Node.js with npm. Nix users can get all of them with `devenv shell`, which is optional.

1. Create a token at [wokwi.com/dashboard/ci](https://wokwi.com/dashboard/ci) and export it.

   ```bash
   export WOKWI_CLI_TOKEN=YOUR_TOKEN
   ```

   If `SECRETSPEC_PROVIDER` is set, the `justfile` runs the Wokwi CLI through [SecretSpec](https://secretspec.dev/), and the `devenv shell` sets it to Bitwarden (`bw://`) and bundles SecretSpec. To use your exported variable instead, set the provider to `env`, for example `SECRETSPEC_PROVIDER=env just scenario haze`.

2. Download the Wokwi CLI. This recipe fetches the Linux x64 build into `~/.wokwi/bin`.

   ```bash
   just wokwi-install
   ```

3. Build the firmware and run the haze scenario, where the potentiometer for PM2.5 moves to 0.85 of its travel and reads 255 µg/m³.

   ```bash
   just scenario haze
   ```

   The run ends with lines like these:

   ```text
   AIR=BERBAHAYA FORECAST3H=BERBAHAYA pm25=255.0 gas=0 gas_adc=0 trend=0.00 temp=30.0 hum=90.0
   [Haze is classified BERBAHAYA and the alarm path runs] Expected text matched: "AIR=BERBAHAYA"
   [Haze is classified BERBAHAYA and the alarm path runs] expect-pin esp:27 == 1
   [Haze is classified BERBAHAYA and the alarm path runs] expect-pin esp:25 == 0
   [Haze is classified BERBAHAYA and the alarm path runs] Scenario completed successfully
   ```

   The red LED on pin 27 is on, and the green LED on pin 25 is off.

Run `just test` for the three quick scenarios (`clean-air`, `moderate-air`, `haze`). Run `just scenario mqtt-roundtrip` for the MQTT test, which takes about 4.5 minutes, close to the 5 minute session limit.

## Opening the dashboard

Node-RED shows the readings from the telemetry topic and sends your mute switch to the command topic, so start it with this command and open [localhost:1880/dashboard](http://localhost:1880/dashboard/).

```bash
just dashboard
```

The widgets move while a simulation runs, so start `just scenario mqtt-roundtrip` in a second terminal to see them update. Flip the **Mute alarm** switch to send `mute` or `unmute` to the device.

## Retraining the model

Run `just setup` once, then `just train`. The script downloads the UCI Beijing Multi-Site Air Quality data and trains the forest, then it writes the C header `firmware/include/air_model.h`, and the scores go to `ml/reports/metrics.json`. Rebuild with `just build` to put the new model in the firmware.

## Finding your way around

| Folder | What it holds |
|:---|:---|
| `firmware/` | ESP32 code (`src/main.cpp`), the Wokwi circuit (`diagram.json`), and the scenarios |
| `ml/` | Training script and evaluation scores |
| `dashboard/` | The Node-RED flow and its settings |
| `LICENSES/` | License texts for the REUSE specification |

Run `just --list` to see every command.

## Knowing the limits

- Two potentiometers stand in for the PM2.5 and gas sensors, so no real sensor was tested.
- The model learned from Beijing data, not Indonesian data, so treat its scores as a demonstration of the method.
- One demo hour is 6 seconds, because the model learned from hourly data, and a real node would read once per hour.
- The simulation sends readings to the public broker `broker.hivemq.com`. The topic names sit in the public code, so anyone who reads them can mute the alarm, and real use needs a private broker with TLS.

## Credits and license

The code is under the Apache License 2.0 (see `LICENSE`). Run `reuse lint` to check the license data.

The training data is Chen, S. (2017), [Beijing Multi-Site Air Quality](https://doi.org/10.24432/C5RK5G), UCI Machine Learning Repository, under CC BY 4.0.
