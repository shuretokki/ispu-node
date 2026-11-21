#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "air_model.h"

constexpr uint8_t PIN_DHT = 15;
constexpr uint8_t PIN_PM = 34;
constexpr uint8_t PIN_GAS = 35;
constexpr uint8_t PIN_LED_GREEN = 25;
constexpr uint8_t PIN_LED_YELLOW = 26;
constexpr uint8_t PIN_LED_RED = 27;
constexpr uint8_t PIN_BUZZER = 4;
constexpr uint8_t PIN_SERVO = 13;

constexpr float MAX_PM25_UGM3 = 300.0f;
constexpr float MAX_GAS_PPM = 1000.0f;
constexpr uint32_t SAMPLE_MS = 2000;
constexpr size_t WINDOW = 10;
// The model learned hourly data, so the demo treats 6 seconds as one hour.
// A real sensor node would sample once per hour and set this to 3600000.
constexpr uint32_t DEMO_HOUR_MS = 6000;
constexpr float TREND_LIMIT = 50.0f;
constexpr float DELTA_LIMIT = 100.0f;
constexpr uint8_t DOWNGRADE_AFTER = 3;
constexpr uint32_t WIFI_RETRY_MS = 15000;

const char *WIFI_SSID = "Wokwi-GUEST";
const char *MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char *TOPIC_TELEMETRY = "ispu-node-4fe22d46/telemetry";
const char *TOPIC_COMMAND = "ispu-node-4fe22d46/command";

const char *CLASS_NAMES[] = {"BAIK", "SEDANG", "TIDAK_SEHAT", "BERBAHAYA"};
const int VENT_ANGLE[] = {0, 30, 60, 90};

DHT dht(PIN_DHT, DHT22);
Servo vent;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

float pmHistory[WINDOW];
uint32_t timeHistory[WINDOW];
size_t historyCount = 0;
size_t historyHead = 0;

int currentClass = 0;
int forecastClass = 0;
uint8_t lowerStreak = 0;
bool muted = false;
uint32_t lastSample = 0;
uint32_t lastMqttTry = 0;
uint32_t lastWifiTry = 0;
bool wifiStarted = false;
int lastGasAdc = 0;

float readPm25() {
  return analogRead(PIN_PM) * MAX_PM25_UGM3 / 4095.0f;
}

float readGasPpm() {
  lastGasAdc = analogRead(PIN_GAS);
  return lastGasAdc * MAX_GAS_PPM / 4095.0f;
}

void pmTrends(float pm, uint32_t now, float &trendPerHour, float &deltaOneHour) {
  pmHistory[historyHead] = pm;
  timeHistory[historyHead] = now;
  historyHead = (historyHead + 1) % WINDOW;
  if (historyCount < WINDOW) {
    historyCount++;
  }
  trendPerHour = 0.0f;
  deltaOneHour = 0.0f;
  if (historyCount < 2) {
    return;
  }
  size_t oldest = (historyHead + WINDOW - historyCount) % WINDOW;
  float hours = (now - timeHistory[oldest]) / static_cast<float>(DEMO_HOUR_MS);
  if (hours > 0) {
    trendPerHour = (pm - pmHistory[oldest]) / hours;
  }
  for (size_t back = 2; back <= historyCount; back++) {
    size_t index = (historyHead + WINDOW - back) % WINDOW;
    if (now - timeHistory[index] >= DEMO_HOUR_MS) {
      deltaOneHour = pm - pmHistory[index];
      break;
    }
  }
}

int ispuClass(float pm, float gas) {
  int pmClass = pm <= 15.5f ? 0 : pm <= 55.4f ? 1 : pm <= 150.4f ? 2 : 3;
  int gasClass = gas <= 200.0f ? 0 : gas <= 400.0f ? 1 : gas <= 800.0f ? 2 : 3;
  return max(pmClass, gasClass);
}

int applyHysteresis(int predicted) {
  if (predicted >= currentClass) {
    lowerStreak = 0;
    return predicted;
  }
  if (++lowerStreak >= DOWNGRADE_AFTER) {
    lowerStreak = 0;
    return currentClass - 1;
  }
  return currentClass;
}

void driveOutputs(int airClass) {
  digitalWrite(PIN_LED_GREEN, airClass == 0);
  digitalWrite(PIN_LED_YELLOW, airClass == 1 || airClass == 2);
  digitalWrite(PIN_LED_RED, airClass >= 2);
  vent.write(VENT_ANGLE[airClass]);
  if (muted || airClass < 2) {
    noTone(PIN_BUZZER);
  } else if (airClass == 2) {
    tone(PIN_BUZZER, 1000, 150);
  } else {
    tone(PIN_BUZZER, 2000);
  }
}

void onCommand(char *, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }
  if (message == "mute") {
    muted = true;
  } else if (message == "unmute") {
    muted = false;
  }
}

void connectNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiStarted || millis() - lastWifiTry > WIFI_RETRY_MS) {
      wifiStarted = true;
      lastWifiTry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, "", 6);
    }
    return;
  }
  if (!mqtt.connected() && millis() - lastMqttTry > 5000) {
    lastMqttTry = millis();
    String clientId = "ispu-node-" + String(random(0xffff), HEX);
    if (mqtt.connect(clientId.c_str())) {
      mqtt.subscribe(TOPIC_COMMAND);
    }
  }
}

void publishTelemetry(float pm, float gas, float trend, float temp, float humidity) {
  // The class and status fields keep the current class, and the forecast fields carry the AI output.
  if (!mqtt.connected()) {
    return;
  }
  JsonDocument doc;
  doc["pm25_ugm3"] = pm;
  doc["gas_ppm"] = gas;
  doc["pm25_trend_ph"] = trend;
  doc["temp_c"] = temp;
  doc["humidity_pct"] = humidity;
  doc["class"] = currentClass;
  doc["status"] = CLASS_NAMES[currentClass];
  doc["forecast_class"] = forecastClass;
  doc["forecast_3h"] = CLASS_NAMES[forecastClass];
  doc["muted"] = muted;
  char buffer[256];
  size_t size = serializeJson(doc, buffer);
  mqtt.publish(TOPIC_TELEMETRY, reinterpret_cast<const uint8_t *>(buffer), size, false);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  dht.begin();
  vent.attach(PIN_SERVO);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onCommand);
  driveOutputs(0);
  Serial.println("Air quality monitor ready");
}

void loop() {
  connectNetwork();
  mqtt.loop();

  uint32_t now = millis();
  if (now - lastSample < SAMPLE_MS) {
    return;
  }
  lastSample = now;

  float pm = readPm25();
  float gas = readGasPpm();
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  if (isnan(temp) || isnan(humidity)) {
    Serial.println("SENSOR_ERROR");
    return;
  }

  float trend = 0.0f;
  float delta = 0.0f;
  pmTrends(pm, now, trend, delta);
  int16_t features[] = {
      static_cast<int16_t>(lroundf(pm)),
      static_cast<int16_t>(lroundf(constrain(trend, -TREND_LIMIT, TREND_LIMIT) * 10.0f)),
      static_cast<int16_t>(lroundf(constrain(delta, -DELTA_LIMIT, DELTA_LIMIT))),
      static_cast<int16_t>(lroundf(temp)),
      static_cast<int16_t>(lroundf(humidity)),
  };
  forecastClass = constrain(air_predict(features, 5), 0, 3);
  currentClass = constrain(applyHysteresis(ispuClass(pm, gas)), 0, 3);
  driveOutputs(currentClass);
  publishTelemetry(pm, gas, trend, temp, humidity);

  Serial.printf("AIR=%s FORECAST3H=%s pm25=%.1f gas=%.0f gas_adc=%d trend=%.2f temp=%.1f hum=%.1f\n",
                CLASS_NAMES[currentClass], CLASS_NAMES[forecastClass], pm, gas, lastGasAdc, trend, temp, humidity);
}
