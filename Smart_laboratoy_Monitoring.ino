/*
  SCS-RG Multi-Hazard Node: Auto-Calibrating + Live Detailed MQTT Diagnostics
  + Extended Telemetry (Button State, Sensor State, WiFi State, Time Sent)
  Sensors: MQ-2, Flame, MQ-135, MQ-7, HC-SR505, AHT10, Water Level, Push Button
  Actuators: RGB LED (Common Cathode) + Active Buzzer + Relay
  Network: WiFi + MQTT (PubSubClient) + NTP Time

  NOTE: Every value published to MQTT is now also echoed verbatim to the
  Serial Monitor (see the "MQTT PUBLISH" blocks) so the exact bytes sent to
  HiveMQ are visible locally, in addition to the human-readable dashboard.
*/

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

// ==========================================
// --- NETWORK & MQTT CONFIGURATION ---
// ==========================================
const char* WIFI_SSID     = "line9";
const char* WIFI_PASSWORD = "6969okok";

const char* MQTT_BROKER       = "broker.hivemq.com";
const int   MQTT_PORT         = 1883;
const char* MQTT_TOPIC        = "koba-samsu";          // Main telemetry payload
const char* MQTT_STATUS_TOPIC = "koba-samsu/status";   // Real-time lifecycle events / logs
const char* MQTT_RELAY_TOPIC  = "koba-samsu/relay";    // Relay status

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttReconnectAttempt = 0;

// NTP Time Config (Adjust GMT offset for your timezone, e.g., 21600 for +6 hrs)
const long  gmtOffset_sec = 21600;
const int   daylightOffset_sec = 0;
const char* ntpServer = "pool.ntp.org";

// ==========================================
// --- SENSOR & ACTUATOR PINS ---
// ==========================================
const int MQ2_PIN       = 34;
const int FLAME_PIN     = 35;
const int MQ135_PIN     = 32;
const int MQ7_PIN       = 33;
const int WATER_PIN     = 36;  // VP Pin (ADC1_CH0)
const int PIR_PIN       = 13;
const int BUTTON_PIN    = 16;

const int RGB_R_PIN     = 26;
const int RGB_G_PIN     = 25;
const int RGB_B_PIN     = 27;
const int BUZZER_PIN    = 14;
const int RELAY_PIN     = 4;   // Active-low relay

const bool isCommonCathode = true;

// Water level calibration (12-bit ADC range: 0 - 4095)
const int WATER_MIN_RAW = 500;
const int WATER_MAX_RAW = 2800;

Adafruit_AHTX0 aht;
bool ahtConnected = false;

int mq2Base, mq2Warn, mq2Crit;
int flameBase, flameWarn, flameCrit;
int mq135Base, mq135Warn, mq135Crit;
int mq7Base, mq7Warn, mq7Crit;

const char* ROOM_IDS[3]   = {"lab01", "lab02", "lab03"};
const char* ROOM_NAMES[3] = {"IoT Lab", "Robotics Lab", "Server Room"};

// Dynamic Global State Variables
String currentSensorState = "on";

// ==========================================
// --- SENSOR READING STRUCT ---
// (Defined here, before any function, so Arduino's auto-generated
//  function prototypes never reference this type before it exists.)
// ==========================================
struct SensorReadings {
  int mq2Raw;
  int flameRaw;
  int mq135Raw;
  int mq7Raw;
  int waterRaw;
  int waterPercent;
  int pirState;
  float tempC;
  float hum;
  bool ahtValid;
  bool buttonPressed;
};

// ==========================================
// --- HELPER: mirror an MQTT publish to Serial ---
// ==========================================
// Prints the exact topic + exact payload bytes that were (or will be)
// published, so the Serial Monitor always matches what HiveMQ receives.
void mirrorToSerial(const char* topic, const char* payload) {
  Serial.println(F("---- MQTT PUBLISH -------------------------------------------"));
  Serial.print(F("TOPIC   : "));
  Serial.println(topic);
  Serial.print(F("PAYLOAD : "));
  Serial.println(payload);
  Serial.println(F("---------------------------------------------------------------"));
}

// Wrapper so every publish site mirrors automatically instead of relying on
// each call site to remember to print it.
bool publishAndMirror(const char* topic, const char* payload, bool retained = false) {
  mirrorToSerial(topic, payload);
  if (!mqttClient.connected()) {
    Serial.println(F("[WARN] MQTT not connected - payload above was NOT sent to broker."));
    return false;
  }
  return mqttClient.publish(topic, payload, retained);
}

// Helper: Get formatted time
String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "00:00AM";
  }
  char timeStringBuff[15];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%I:%M%p", &timeinfo);
  return String(timeStringBuff);
}

// Helper: Stream step-by-step logs to MQTT Status topic
void publishStatusLog(const char* phase, const char* message, int progress = -1) {
  char logPayload[256];
  if (progress >= 0) {
    snprintf(logPayload, sizeof(logPayload),
      "{\"node_id\":\"ESP32-01\",\"phase\":\"%s\",\"message\":\"%s\",\"progress\":%d,\"rssi\":%d}",
      phase, message, progress, WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  } else {
    snprintf(logPayload, sizeof(logPayload),
      "{\"node_id\":\"ESP32-01\",\"phase\":\"%s\",\"message\":\"%s\",\"rssi\":%d}",
      phase, message, WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  }
  // Human-readable one-liner for quick scanning...
  Serial.printf("[%s] %s\n", phase, message);
  // ...and the exact JSON that goes (or would go) to koba-samsu/status
  publishAndMirror(MQTT_STATUS_TOPIC, logPayload, false);
}

void setRGB(bool r, bool g, bool b) {
  if (isCommonCathode) {
    digitalWrite(RGB_R_PIN, r); digitalWrite(RGB_G_PIN, g); digitalWrite(RGB_B_PIN, b);
  } else {
    digitalWrite(RGB_R_PIN, !r); digitalWrite(RGB_G_PIN, !g); digitalWrite(RGB_B_PIN, !b);
  }
}

void setupWiFi() {
  Serial.print("\nConnecting to WiFi SSID: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts > 40) ESP.restart(); // Fail-safe reboot if WiFi hangs completely
  }
  Serial.println("\nWiFi Connected!");

  // Init NTP Time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

// Non-blocking MQTT Reconnect
void reconnectMQTT() {
  if (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Node-" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str(), MQTT_STATUS_TOPIC, 0, true, "{\"node_id\":\"ESP32-01\",\"phase\":\"OFFLINE\",\"message\":\"Node abruptly disconnected!\"}")) {
      Serial.println(" Connected!");
      publishStatusLog("SYSTEM", "MQTT Broker Connection Established");
    } else {
      Serial.print(" Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" (Will retry in background)");
    }
  }
}

// ==========================================
// --- SENSOR READING UTILITY ---
// ==========================================
SensorReadings sampleSensors() {
  SensorReadings s;
  s.mq2Raw = 0; s.flameRaw = 0; s.mq135Raw = 0; s.mq7Raw = 0; s.waterRaw = 0;

  for (int i = 0; i < 10; i++) {
    s.mq2Raw   += analogRead(MQ2_PIN);
    s.flameRaw += analogRead(FLAME_PIN);
    s.mq135Raw += analogRead(MQ135_PIN);
    s.mq7Raw   += analogRead(MQ7_PIN);
    s.waterRaw += analogRead(WATER_PIN);
    delay(2);
  }
  s.mq2Raw /= 10; s.flameRaw /= 10; s.mq135Raw /= 10; s.mq7Raw /= 10; s.waterRaw /= 10;

  s.waterPercent = map(s.waterRaw, WATER_MIN_RAW, WATER_MAX_RAW, 0, 100);
  s.waterPercent = constrain(s.waterPercent, 0, 100);

  s.pirState = digitalRead(PIR_PIN);
  s.buttonPressed = (digitalRead(BUTTON_PIN) == LOW);

  if (ahtConnected) {
    sensors_event_t humidity, temp;
    s.ahtValid = aht.getEvent(&humidity, &temp);
    s.tempC = temp.temperature;
    s.hum = humidity.relative_humidity;
  } else {
    s.ahtValid = false;
    s.tempC = 0.0;
    s.hum = 0.0;
  }
  return s;
}

// ==========================================
// --- ON-DEMAND DIAGNOSTIC TEST (BUTTON) ---
// ==========================================
void runFullSensorDiagnostic() {
  currentSensorState = "RECHECK-ING";
  publishStatusLog("DIAGNOSTIC", "Button Pressed! Initiating Full Diagnostic Self-Test...");

  for (int f = 0; f < 3; f++) {
    setRGB(HIGH, HIGH, LOW);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(80);
    setRGB(LOW, LOW, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    delay(80);
  }

  SensorReadings s = sampleSensors();

  bool mq2Ok    = (s.mq2Raw > 50 && s.mq2Raw < 4000);
  bool flameOk  = (s.flameRaw > 100);
  bool mq135Ok  = (s.mq135Raw > 50 && s.mq135Raw < 4000);
  bool mq7Ok    = (s.mq7Raw > 50 && s.mq7Raw < 4000);
  bool waterOk  = (s.waterRaw >= 0 && s.waterRaw <= 4095);
  bool ahtOk    = s.ahtValid;

  bool allPass = mq2Ok && flameOk && mq135Ok && mq7Ok && waterOk && ahtOk;
  currentSensorState = allPass ? "on" : "OFF";

  char diagPayload[800];
  snprintf(diagPayload, sizeof(diagPayload),
    "{\"event\":\"DIAGNOSTIC_RESULT\",\"node_id\":\"ESP32-01\",\"rssi\":%d,"
    "\"wifi_state\":\"CONNECTED\",\"button_state\":\"ON\","
    "\"sensor_state\":\"%s\","
    "\"diagnostics\":{"
    "\"mq2\":{\"raw\":%d,\"base\":%d,\"crit\":%d,\"status\":\"%s\"},"
    "\"mq7\":{\"raw\":%d,\"base\":%d,\"crit\":%d,\"status\":\"%s\"},"
    "\"mq135\":{\"raw\":%d,\"base\":%d,\"crit\":%d,\"status\":\"%s\"},"
    "\"flame\":{\"raw\":%d,\"base\":%d,\"crit\":%d,\"status\":\"%s\"},"
    "\"water\":{\"raw\":%d,\"pct\":%d,\"status\":\"%s\"},"
    "\"aht10\":{\"temp\":%.1f,\"hum\":%.1f,\"status\":\"%s\"},"
    "\"pir\":{\"state\":%d,\"status\":\"OK\"}"
    "}}",
    WiFi.RSSI(), currentSensorState.c_str(),
    s.mq2Raw, mq2Base, mq2Crit, mq2Ok ? "PASS" : "FAIL",
    s.mq7Raw, mq7Base, mq7Crit, mq7Ok ? "PASS" : "FAIL",
    s.mq135Raw, mq135Base, mq135Crit, mq135Ok ? "PASS" : "FAIL",
    s.flameRaw, flameBase, flameCrit, flameOk ? "PASS" : "FAIL",
    s.waterRaw, s.waterPercent, waterOk ? "PASS" : "FAIL",
    s.tempC, s.hum, ahtOk ? "PASS" : "FAIL",
    s.pirState
  );

  // Exact bytes sent to both the status topic and the main telemetry topic
  publishAndMirror(MQTT_STATUS_TOPIC, diagPayload, false);
  publishAndMirror(MQTT_TOPIC, diagPayload, false);
  Serial.println("\n>>> FULL DIAGNOSTIC TELEMETRY PUBLISHED TO MQTT <<<\n");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();
  Wire.setTimeOut(150); // Prevent I2C lockups from crashing the ESP32

  pinMode(RGB_R_PIN, OUTPUT); pinMode(RGB_G_PIN, OUTPUT);
  pinMode(RGB_B_PIN, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  analogReadResolution(12);

  setupWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(2048);

  // Initial blocking reconnect to ensure start state
  while (!mqttClient.connected()) {
    reconnectMQTT();
    delay(1000);
  }

  if (!aht.begin()) {
    ahtConnected = false;
    publishStatusLog("INIT", "ERROR: AHT10 Sensor Not Detected!");
  } else {
    ahtConnected = true;
    publishStatusLog("INIT", "SUCCESS: AHT10 Temperature/Humidity Initialized");
  }

  publishStatusLog("WARMUP", "Starting 30-second Heating Element Warmup...");
  for (int i = 30; i > 0; i--) {
    mqttClient.loop(); // Prevent MQTT ping timeout
    setRGB(LOW, LOW, HIGH); delay(500);
    setRGB(LOW, LOW, LOW);  delay(500);
    char warmupMsg[64];
    snprintf(warmupMsg, sizeof(warmupMsg), "Warming up MQ elements: %d seconds remaining", i);
    publishStatusLog("WARMUP", warmupMsg, i);
  }

  publishStatusLog("CALIBRATION", "Sampling Room Baselines (Do not trigger gas/fire)...");
  setRGB(HIGH, LOW, HIGH);

  long mq2Sum = 0, flameSum = 0, mq135Sum = 0, mq7Sum = 0;
  for (int i = 0; i < 100; i++) {
    mqttClient.loop(); // Prevent MQTT ping timeout
    mq2Sum += analogRead(MQ2_PIN);
    flameSum += analogRead(FLAME_PIN);
    mq135Sum += analogRead(MQ135_PIN);
    mq7Sum += analogRead(MQ7_PIN);
    if (i % 20 == 0) publishStatusLog("CALIBRATION", "Sampling baseline registers...", i);
    delay(50);
  }

  mq2Base   = mq2Sum / 100; flameBase = flameSum / 100;
  mq135Base = mq135Sum / 100; mq7Base   = mq7Sum / 100;

  mq2Warn   = mq2Base + 400;    mq2Crit   = mq2Base + 800;
  mq135Warn = mq135Base + 300;  mq135Crit = mq135Base + 700;
  mq7Warn   = mq7Base + 250;    mq7Crit   = mq7Base + 500;
  flameWarn = flameBase - 1000; flameCrit = flameBase - 2000;

  publishStatusLog("CALIBRATION", "Calibration Complete. Dynamic Thresholds Locked.");
  setRGB(LOW, LOW, LOW);
}

void loop() {
  // Non-blocking MQTT Reconnect
  if (!mqttClient.connected()) {
    if (millis() - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = millis();
      reconnectMQTT();
    }
  } else {
    mqttClient.loop();
  }

  SensorReadings s = sampleSensors();

  // Evaluate Risk & Build Triggers (Memory safe using char array)
  int warningCount = 0, criticalCount = 0;
  char activeTriggers[128] = "";
  char jsonTriggers[256] = "";

  auto addTrigger = [&](const char* name) {
    if (strlen(activeTriggers) > 0) strncat(activeTriggers, ", ", sizeof(activeTriggers) - strlen(activeTriggers) - 1);
    strncat(activeTriggers, name, sizeof(activeTriggers) - strlen(activeTriggers) - 1);

    if (strlen(jsonTriggers) > 0) strncat(jsonTriggers, ",", sizeof(jsonTriggers) - strlen(jsonTriggers) - 1);
    strncat(jsonTriggers, "\"", sizeof(jsonTriggers) - strlen(jsonTriggers) - 1);
    strncat(jsonTriggers, name, sizeof(jsonTriggers) - strlen(jsonTriggers) - 1);
    strncat(jsonTriggers, "\"", sizeof(jsonTriggers) - strlen(jsonTriggers) - 1);
  };

  if (s.mq2Raw > mq2Crit) { criticalCount++; addTrigger("MQ2 Gas (CRIT)"); }
  else if (s.mq2Raw > mq2Warn) { warningCount++; addTrigger("MQ2 Gas"); }

  if (s.mq135Raw > mq135Crit) { criticalCount++; addTrigger("MQ135 Air (CRIT)"); }
  else if (s.mq135Raw > mq135Warn) { warningCount++; addTrigger("MQ135 Air"); }

  if (s.mq7Raw > mq7Crit) { criticalCount++; addTrigger("MQ7 CO (CRIT)"); }
  else if (s.mq7Raw > mq7Warn) { warningCount++; addTrigger("MQ7 CO"); }

  if (s.flameRaw < flameCrit) { criticalCount++; addTrigger("Flame IR (CRIT)"); }
  else if (s.flameRaw < flameWarn) { warningCount++; addTrigger("Flame IR"); }

  if (s.tempC > 42.0) { criticalCount++; addTrigger("Temp High (CRIT)"); }
  else if (s.tempC > 34.0) { warningCount++; addTrigger("Temp High"); }

  int systemState = 0; // NORMAL
  if (criticalCount > 0 || warningCount > 1) systemState = 2; // EMERGENCY
  else if (warningCount == 1) systemState = 1;               // CRITICAL

  const char* stateText = (systemState == 0) ? "NORMAL" : (systemState == 1) ? "CRITICAL" : "EMERGENCY";
  const char* wifiStateStr = (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "DISCONNECTED";
  const char* buttonStateStr = s.buttonPressed ? "ON" : "OFF";
  String timeStampStr = getCurrentTime();

  bool shouldBeActive = (systemState != 0);
  digitalWrite(RELAY_PIN, shouldBeActive ? LOW : HIGH);
  const char* relayText = shouldBeActive ? "ON" : "OFF";

  // Relay status - exact payload mirrored to Serial as well
  publishAndMirror(MQTT_RELAY_TOPIC, relayText, true);

  // Build JSON Payload
  char roomBuf[3][400];
  for (int r = 0; r < 3; r++) {
    snprintf(roomBuf[r], sizeof(roomBuf[r]),
      "{\"room_id\":\"%s\",\"room_name\":\"%s\","
      "\"mq2\":%d,\"mq7\":%d,\"mq135\":%d,\"flame\":%d,"
      "\"water_pct\":%d,\"water_raw\":%d,\"temp\":%.1f,\"hum\":%.1f,\"pir\":%s,"
      "\"state\":%d,\"state_text\":\"%s\",\"warnings\":[%s]}",
      ROOM_IDS[r], ROOM_NAMES[r], s.mq2Raw, s.mq7Raw, s.mq135Raw, s.flameRaw,
      s.waterPercent, s.waterRaw, s.tempC, s.hum, s.pirState == HIGH ? "true" : "false",
      systemState, stateText, jsonTriggers
    );
  }

  char jsonPayload[2048];
  snprintf(jsonPayload, sizeof(jsonPayload),
    "{\"node_id\":\"ESP32-01\",\"rssi\":%d,\"relay\":\"%s\","
    "\"button_state\":\"%s\",\"sensor_state\":\"%s\",\"wifi_state\":\"%s\",\"time_sent\":\"%s\","
    "\"rooms\":[%s,%s,%s]}",
    WiFi.RSSI(), relayText, buttonStateStr, currentSensorState.c_str(),
    wifiStateStr, timeStampStr.c_str(), roomBuf[0], roomBuf[1], roomBuf[2]
  );

  // Main telemetry - exact bytes sent to koba-samsu, mirrored to Serial
  publishAndMirror(MQTT_TOPIC, jsonPayload, false);

  // Output human-readable Serial Dashboard (in addition to the raw JSON above)
  Serial.println("┌─────────────────────────────────────────────────────────────┐");
  Serial.printf("│  SCS-RG TELEMETRY DASHBOARD        TIME: %-19s│\n", timeStampStr.c_str());
  Serial.println("├─────────────────────────────────────────────────────────────┤");
  Serial.printf("│  MQ-2 (Gas/Smoke) : %4d  [Crit: >%d]                 │\n", s.mq2Raw, mq2Crit);
  Serial.printf("│  MQ-7 (CO)        : %4d  [Crit: >%d]                 │\n", s.mq7Raw, mq7Crit);
  Serial.printf("│  MQ-135 (Air Qual): %4d  [Crit: >%d]                 │\n", s.mq135Raw, mq135Crit);
  Serial.printf("│  Flame Sensor (IR): %4d  [Crit: <%d]                 │\n", s.flameRaw, flameCrit);
  Serial.printf("│  Water Level      : %3d%%  (Raw: %4d)                     │\n", s.waterPercent, s.waterRaw);
  Serial.printf("│  Temperature      : %4.1f C                               │\n", s.tempC);
  Serial.printf("│  Humidity         : %4.1f %%                               │\n", s.hum);
  Serial.printf("│  PIR Occupancy    : %-4s                                    │\n", s.pirState == HIGH ? "YES" : "NO");
  Serial.printf("│  RELAY STATE      : %-4s                                    │\n", relayText);
  Serial.println("├─────────────────────────────────────────────────────────────┤");
  Serial.printf("│  SYSTEM STATE     : %-40s│\n", stateText);
  Serial.printf("│  WARNING REASON   : %-40s│\n", strlen(activeTriggers) > 0 ? activeTriggers : "None");
  Serial.printf("│  Button State     : %-40s│\n", buttonStateStr);
  Serial.printf("│  SENSOR STATE     : %-40s│\n", currentSensorState.c_str());
  Serial.printf("│  WIFI STATE       : %-40s│\n", wifiStateStr);
  Serial.println("└─────────────────────────────────────────────────────────────┘\n");

  // Actuation & Button Check Loop
  unsigned long startWait = millis();
  while (millis() - startWait < 1500) {
    if (mqttClient.connected()) mqttClient.loop();

    // Check for button press to run diagnostic
    if (digitalRead(BUTTON_PIN) == LOW) {
      runFullSensorDiagnostic();
      break;
    }

    bool blinkState = (millis() % 500) < 250;
    if (systemState == 0) {
      setRGB(LOW, LOW, HIGH);
      digitalWrite(BUZZER_PIN, LOW);
    } else if (systemState == 1) {
      if (blinkState) setRGB(HIGH, HIGH, LOW);
      else setRGB(LOW, LOW, LOW);
      digitalWrite(BUZZER_PIN, LOW);
    } else if (systemState == 2) {
      if (blinkState) {
        setRGB(HIGH, LOW, LOW);
        digitalWrite(BUZZER_PIN, HIGH);
      } else {
        setRGB(LOW, LOW, LOW);
        digitalWrite(BUZZER_PIN, LOW);
      }
    }
    delay(10);
  }
}
