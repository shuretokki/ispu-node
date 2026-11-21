set default-list := true
set shell := ["bash", "-euo", "pipefail", "-c"]

mod? docs 'docs/local'

wokwi := env("HOME") / ".wokwi/bin/wokwi-cli"

# Token comes from secretspec when SECRETSPEC_PROVIDER is set, devenv do that. Else export WOKWI_CLI_TOKEN yourself.
with_token := if env("SECRETSPEC_PROVIDER", "") != "" { "secretspec run --reason 'Run the Wokwi CLI' --" } else { "" }

# Install deps for the model.
[group("model")]
setup:
    {{ require("uv") }} sync --project ml

# Train it, then write air_model.h.
[group("model")]
train:
    cd ml && {{ require("uv") }} run python -I train.py

# Build the firmware.
[group("firmware")]
build:
    {{ require("pio") }} run -d firmware

# Run the sim and check the serial output.
[group("firmware")]
sim: build _wokwi-installed
    {{ with_token }} {{ wokwi }} firmware --timeout 30000 --expect-text "Air quality monitor ready" --fail-text "SENSOR_ERROR"

# Quick scenarios only. The mqtt-roundtrip is 4.5 minutes, run it alone.
[group("firmware")]
test: build _wokwi-installed
    for f in firmware/scenarios/*.test.yaml; do [ "$(basename "$f")" = "mqtt-roundtrip.test.yaml" ] && continue; {{ with_token }} {{ wokwi }} firmware --scenario "scenarios/$(basename "$f")" --timeout 60000; done

# One scenario by name, like: just scenario clean-air
[group("firmware")]
scenario name="haze": build _wokwi-installed
    {{ with_token }} {{ wokwi }} firmware --scenario "scenarios/{{ name }}.test.yaml" --timeout 60000

# Node-RED dashboard on localhost:1880/dashboard/.
[group("dashboard")]
dashboard:
    cd dashboard && {{ require("npm") }} install --silent && {{ require("npx") }} --no-install node-red --userDir . flows.json

# Get the Wokwi CLI into ~/.wokwi/bin.
[group("wokwi")]
wokwi-install:
    mkdir -p "$(dirname "{{ wokwi }}")"
    curl --fail --location --silent --output "{{ wokwi }}" https://github.com/wokwi/wokwi-cli/releases/latest/download/wokwi-cli-linuxstatic-x64
    chmod +x "{{ wokwi }}"

# Make wokwi-web folder to paste in a wokwi.com project.
[group("wokwi")]
wokwi-web:
    mkdir -p wokwi-web
    cp firmware/src/main.cpp wokwi-web/sketch.ino
    cp firmware/include/air_model.h firmware/diagram.json wokwi-web/
    cp firmware/README.md wokwi-web/README.md
    printf '%s\n' "DHT sensor library@1.4.7" "Adafruit Unified Sensor@1.1.15" "ESP32Servo@3.2.1" "PubSubClient@2.8.0" "ArduinoJson@7.4.3" > wokwi-web/libraries.txt

# Check every secret is in the provider, needs SecretSpec.
[group("wokwi")]
secrets-check:
    {{ require("secretspec") }} check --reason "Owner checks the project secrets with just"

[private]
_wokwi-installed:
    @[ -x "{{ wokwi }}" ] || { echo "Wokwi CLI not found at {{ wokwi }}. Run: just wokwi-install"; exit 1; }
