
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <esp_system.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ============================================================
// FIREGUARD - ESP32 + FREERTOS + MQ-2 + DHT22 + MQTT
// Task MQTT/WiFi: Core 0
// Task Read Sensor: Core 1
// Task Alert/Actuator: Core 1
// ============================================================

// ---------------- WiFi / MQTT ----------------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_SERVER = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char* TOPIC_BASE = "firealarm/congminh_thcn2";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ---------------- PIN ----------------
#define MQ2_PIN       34
#define DHT_PIN       15
#define DHT_TYPE      DHT22

#define FAN_PIN       26
#define PUMP_PIN      27
#define BUZZER_PIN    25

#define RELAY_ON      HIGH
#define RELAY_OFF     LOW

DHT dht(DHT_PIN, DHT_TYPE);

// ---------------- NGUONG CAM BIEN ----------------
const uint32_t SAMPLE_PERIOD_MS = 2000;

const float GAS_ADC_CLEAN = 843.0f;

// Gas > 3000: bat may bom
const int GAS_PUMP_THRESHOLD = 3000;

// Nhiet do > 50: bat quat
const float TEMP_FAN_THRESHOLD = 50.0f;

// Muc nguy hiem
const int GAS_CRITICAL = 6000;
const float TEMP_CRITICAL = 60.0f;

const uint8_t GAS_AVERAGE_COUNT = 3;

// ---------------- DATA STRUCT ----------------
typedef struct {
  uint32_t sequence;
  uint32_t sampleTimeMs;
  int gasRaw;
  int gasIndex;
  float temperatureC;
  bool temperatureValid;
} SensorPacket;

typedef enum : uint8_t {
  ALARM_NORMAL = 0,
  ALARM_GAS_WARNING,
  ALARM_HEAT_WARNING,
  ALARM_FIRE
} AlarmLevel;

typedef struct {
  bool fanOn;
  bool pumpOn;
  bool manualMode;
  AlarmLevel alarmLevel;
} ActuatorReport;

typedef enum : uint8_t {
  CMD_SET_MODE = 0,
  CMD_SET_FAN,
  CMD_SET_PUMP
} CommandType;

typedef struct {
  CommandType type;
  bool value;
} ControlCommand;

// ---------------- FREERTOS QUEUE ----------------
QueueHandle_t dataQueue = nullptr;
QueueHandle_t dataAlert = nullptr;
QueueHandle_t commandQueue = nullptr;
QueueHandle_t stateQueue = nullptr;

// ---------------- TASK HANDLE ----------------
TaskHandle_t taskMqttHandle = nullptr;
TaskHandle_t taskReadHandle = nullptr;
TaskHandle_t taskAlertHandle = nullptr;

// ============================================================
// COMMON FUNCTIONS
// ============================================================

const char* alarmToText(AlarmLevel level) {
  switch (level) {
    case ALARM_GAS_WARNING:
      return "GAS_WARNING";

    case ALARM_HEAT_WARNING:
      return "HEAT_WARNING";

    case ALARM_FIRE:
      return "FIRE";

    default:
      return "NORMAL";
  }
}

void printSensorPacket(const SensorPacket& packet) {
  Serial.println();
  Serial.println("========== FIREGUARD / SENSOR TASK ==========");

  Serial.printf("Lan doc: %lu\n",
                (unsigned long)packet.sequence);

  Serial.printf("MQ-2 ADC raw: %d\n", packet.gasRaw);

  Serial.printf("Gas index (khong phai PPM): %d\n",
                packet.gasIndex);

  if (packet.temperatureValid) {
    Serial.printf("Nhiet do DHT22: %.1f C\n",
                  packet.temperatureC);
  } else {
    Serial.println("Nhiet do DHT22: LOI DOC CAM BIEN");
  }

  Serial.println("=============================================");
}

void publishMqttValue(const char* suffix,
                      const String& value) {
  if (!mqttClient.connected()) return;

  String topic = String(TOPIC_BASE) + "/" + suffix;

  mqttClient.publish(
    topic.c_str(),
    value.c_str(),
    true
  );
}

// ============================================================
// TASK 1: DOC CAM BIEN - CORE 1
// ============================================================

int readGasIndex(int& rawAverage) {
  long total = 0;

  for (uint8_t i = 0; i < GAS_AVERAGE_COUNT; i++) {
    total += analogRead(MQ2_PIN);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  rawAverage = (int)(total / GAS_AVERAGE_COUNT);

  float indexValue =
      ((float)rawAverage - GAS_ADC_CLEAN) * 10000.0f /
      (4095.0f - GAS_ADC_CLEAN);

  if (indexValue < 0.0f) indexValue = 0.0f;
  if (indexValue > 10000.0f) indexValue = 10000.0f;

  return (int)indexValue;
}

void TaskReadSensor(void* pvParameters) {
  (void)pvParameters;

  TickType_t lastWakeTime = xTaskGetTickCount();

  uint32_t sequence = 0;

  for (;;) {
    SensorPacket packet = {};

    packet.sequence = ++sequence;
    packet.sampleTimeMs = millis();

    // Doc MQ-2
    packet.gasIndex = readGasIndex(packet.gasRaw);

    // Doc DHT22
    float measuredTemp = dht.readTemperature();

    if (!isnan(measuredTemp) &&
        measuredTemp >= -40.0f &&
        measuredTemp <= 80.0f) {

      packet.temperatureC = measuredTemp;
      packet.temperatureValid = true;

    } else {
      packet.temperatureC = 0.0f;
      packet.temperatureValid = false;
    }

    // Hien thi Serial
    printSensorPacket(packet);

    // Gui du lieu sang Task MQTT
    if (xQueueSend(dataQueue, &packet, 0) != pdTRUE) {
      SensorPacket discarded;

      xQueueReceive(dataQueue, &discarded, 0);

      xQueueSend(dataQueue, &packet, 0);
    }

    // Gui du lieu sang Task Alert
    xQueueOverwrite(dataAlert, &packet);

    // Chu ky doc 2 giay
    vTaskDelayUntil(
      &lastWakeTime,
      pdMS_TO_TICKS(SAMPLE_PERIOD_MS)
    );
  }
}

// ============================================================
// TASK 2: CANH BAO VA DIEU KHIEN - CORE 1
// ============================================================

void updateBuzzer(AlarmLevel level) {
  static uint32_t lastToggle = 0;
  static bool buzzerOn = false;

  if (level == ALARM_NORMAL) {
    ledcWriteTone(BUZZER_PIN, 0);

    buzzerOn = false;
    lastToggle = millis();

    return;
  }

  uint32_t now = millis();

  uint32_t interval =
      (level == ALARM_FIRE) ? 180 : 350;

  uint16_t frequency =
      (level == ALARM_FIRE) ? 2500 : 1800;

  if (now - lastToggle >= interval) {
    lastToggle = now;

    buzzerOn = !buzzerOn;

    ledcWriteTone(
      BUZZER_PIN,
      buzzerOn ? frequency : 0
    );
  }
}

void TaskAlert(void* pvParameters) {
  (void)pvParameters;

  SensorPacket latestPacket = {};

  bool haveSensorData = false;

  bool gasAlarm = false;
  bool tempAlarm = false;

  bool manualMode = true;

  bool fanManual = false;
  bool pumpManual = false;

  bool lastFanState = false;
  bool lastPumpState = false;

  AlarmLevel lastLevel = ALARM_NORMAL;

  bool lastMode = true;
  bool reportInitialized = false;

  for (;;) {
    bool stateChanged = false;

    // -------- NHAN LENH TU WEBSITE --------
    ControlCommand command;

    while (xQueueReceive(commandQueue, &command, 0) == pdTRUE) {

      if (command.type == CMD_SET_MODE) {
        if (manualMode != command.value) {

          manualMode = command.value;

          fanManual = false;
          pumpManual = false;

          stateChanged = true;
        }
      }

      else if (command.type == CMD_SET_FAN) {
        if (manualMode) {
          fanManual = command.value;
          stateChanged = true;
        }
      }

      else if (command.type == CMD_SET_PUMP) {
        if (manualMode) {
          pumpManual = command.value;
          stateChanged = true;
        }
      }
    }

    // -------- NHAN DU LIEU CAM BIEN --------
    if (xQueueReceive(
          dataAlert,
          &latestPacket,
          pdMS_TO_TICKS(20)
        ) == pdTRUE) {

      haveSensorData = true;

      bool oldGasAlarm = gasAlarm;
      bool oldTempAlarm = tempAlarm;

      // GAS > 3000: MAY BOM BAT
      gasAlarm =
          (latestPacket.gasIndex > GAS_PUMP_THRESHOLD);

      // NHIET DO > 50: QUAT BAT
      tempAlarm =
          latestPacket.temperatureValid &&
          (latestPacket.temperatureC > TEMP_FAN_THRESHOLD);

      // Neu canh bao ket thuc trong MANUAL
      // thi xoa lenh ON cu cua thiet bi tuong ung.
      if (manualMode && oldGasAlarm && !gasAlarm) {
        pumpManual = false;
      }

      if (manualMode && oldTempAlarm && !tempAlarm) {
        fanManual = false;
      }

      stateChanged = true;
    }

    // -------- XAC DINH MUC CANH BAO --------
    AlarmLevel currentLevel = ALARM_NORMAL;

    bool gasCritical =
        haveSensorData &&
        (latestPacket.gasIndex >= GAS_CRITICAL);

    bool tempCritical =
        haveSensorData &&
        latestPacket.temperatureValid &&
        (latestPacket.temperatureC >= TEMP_CRITICAL);

    if (gasCritical ||
        tempCritical ||
        (gasAlarm && tempAlarm)) {

      currentLevel = ALARM_FIRE;

    } else if (gasAlarm) {

      currentLevel = ALARM_GAS_WARNING;

    } else if (tempAlarm) {

      currentLevel = ALARM_HEAT_WARNING;
    }

    // -------- DIEU KHIEN QUAT VA MAY BOM --------
    bool fanState;
    bool pumpState;

    if (manualMode) {

      // MANUAL:
      // Nhiet do cao -> ep bat quat
      // Gas cao -> ep bat may bom

      fanState = fanManual || tempAlarm;
      pumpState = pumpManual || gasAlarm;

    } else {

      // AUTO:
      // Nhiet do cao -> quat
      // Gas cao -> may bom

      fanState = tempAlarm;
      pumpState = gasAlarm;
    }

    // Muc nguy hiem van ep bat thiet bi tuong ung
    if (gasCritical) {
      pumpState = true;
    }

    if (tempCritical) {
      fanState = true;
    }

    // Xuat tin hieu dieu khien relay
    digitalWrite(
      FAN_PIN,
      fanState ? RELAY_ON : RELAY_OFF
    );

    digitalWrite(
      PUMP_PIN,
      pumpState ? RELAY_ON : RELAY_OFF
    );

    // Dieu khien coi bao dong
    updateBuzzer(currentLevel);

    // -------- GUI TRANG THAI SANG TASK MQTT --------
    if (!reportInitialized ||
        stateChanged ||
        fanState != lastFanState ||
        pumpState != lastPumpState ||
        currentLevel != lastLevel ||
        manualMode != lastMode) {

      ActuatorReport report;

      report.fanOn = fanState;
      report.pumpOn = pumpState;
      report.manualMode = manualMode;
      report.alarmLevel = currentLevel;

      xQueueOverwrite(stateQueue, &report);

      lastFanState = fanState;
      lastPumpState = pumpState;

      lastLevel = currentLevel;
      lastMode = manualMode;

      reportInitialized = true;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
// MQTT CALLBACK
// Chi nhan lenh va dua vao commandQueue
// ============================================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
) {
  String receivedTopic(topic);

  String message;
  message.reserve(length + 1);

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  message.trim();
  message.toUpperCase();

  String topicMode =
      String(TOPIC_BASE) + "/cmd/mode";

  String topicFan =
      String(TOPIC_BASE) + "/cmd/fan";

  String topicPump =
      String(TOPIC_BASE) + "/cmd/pump";

  ControlCommand command;

  bool validCommand = false;

  // Lenh chuyen che do
  if (receivedTopic == topicMode) {

    if (message == "MANUAL") {
      command.type = CMD_SET_MODE;
      command.value = true;
      validCommand = true;

    } else if (message == "AUTO") {
      command.type = CMD_SET_MODE;
      command.value = false;
      validCommand = true;
    }
  }

  // Lenh dieu khien quat va bom
  else if (receivedTopic == topicFan ||
           receivedTopic == topicPump) {

    bool turnOn =
        (message == "ON" ||
         message == "1" ||
         message == "TRUE");

    bool turnOff =
        (message == "OFF" ||
         message == "0" ||
         message == "FALSE");

    if (turnOn || turnOff) {

      command.type =
          (receivedTopic == topicFan)
          ? CMD_SET_FAN
          : CMD_SET_PUMP;

      command.value = turnOn;

      validCommand = true;
    }
  }

  // Dua lenh vao Queue
  if (validCommand && commandQueue != nullptr) {

    if (xQueueSend(commandQueue, &command, 0) != pdTRUE) {

      ControlCommand discarded;

      xQueueReceive(commandQueue, &discarded, 0);

      xQueueSend(commandQueue, &command, 0);
    }
  }
}

// ============================================================
// TASK 3: WIFI + MQTT - CORE 0
// ============================================================

void TaskMqtt(void* pvParameters) {
  (void)pvParameters;

  bool wifiBeginCalled = false;

  uint32_t lastWifiAttempt = 0;
  uint32_t lastMqttAttempt = 0;

  for (;;) {

    uint32_t now = millis();

    // -------- KET NOI WIFI --------
    if (WiFi.status() != WL_CONNECTED) {

      if (!wifiBeginCalled ||
          (now - lastWifiAttempt >= 5000)) {

        Serial.println(
          "[Task MQTT] Dang ket noi WiFi..."
        );

        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);

        wifiBeginCalled = true;
        lastWifiAttempt = now;
      }

      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    // -------- KET NOI MQTT --------
    if (!mqttClient.connected() &&
        (now - lastMqttAttempt >= 3000)) {

      lastMqttAttempt = now;

      String clientId =
          "fireguard-" +
          String((uint32_t)ESP.getEfuseMac(), HEX);

      Serial.println(
        "[Task MQTT] Dang ket noi HiveMQ..."
      );

      if (mqttClient.connect(clientId.c_str())) {

        Serial.println(
          "[Task MQTT] MQTT connected!"
        );

        String commandTopic =
            String(TOPIC_BASE) + "/cmd/#";

        mqttClient.subscribe(
          commandTopic.c_str()
        );

      } else {

        Serial.printf(
          "[Task MQTT] MQTT connect failed, state=%d\n",
          mqttClient.state()
        );
      }
    }

    // -------- DUY TRI MQTT --------
    if (mqttClient.connected()) {

      mqttClient.loop();

      // Doc dataQueue va gui du lieu len MQTT
      SensorPacket packet;

      if (xQueueReceive(dataQueue, &packet, 0) == pdTRUE) {

        publishMqttValue(
          "gas",
          String(packet.gasIndex)
        );

        if (packet.temperatureValid) {

          publishMqttValue(
            "temp",
            String(packet.temperatureC, 1)
          );

          publishMqttValue(
            "temp/valid",
            "1"
          );

        } else {

          publishMqttValue(
            "temp/valid",
            "0"
          );
        }

        publishMqttValue(
          "gas/raw",
          String(packet.gasRaw)
        );
      }

      // Doc trang thai thiet bi va gui len website
      ActuatorReport report;

      if (xQueueReceive(stateQueue, &report, 0) == pdTRUE) {

        publishMqttValue(
          "fan/state",
          report.fanOn ? "ON" : "OFF"
        );

        publishMqttValue(
          "pump/state",
          report.pumpOn ? "ON" : "OFF"
        );

        publishMqttValue(
          "mode",
          report.manualMode ? "MANUAL" : "AUTO"
        );

        publishMqttValue(
          "status",
          alarmToText(report.alarmLevel)
        );
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(200);

  Serial.println();
  Serial.println("===== FIREGUARD RTOS START =====");

  pinMode(MQ2_PIN, INPUT);

  pinMode(FAN_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);

  digitalWrite(FAN_PIN, RELAY_OFF);
  digitalWrite(PUMP_PIN, RELAY_OFF);

  analogReadResolution(12);

  dht.begin();

  // Khoi tao PWM cho coi bao dong
  ledcAttach(BUZZER_PIN, 2500, 8);

  ledcWriteTone(BUZZER_PIN, 0);

  // -------- TAO QUEUE --------
  dataQueue = xQueueCreate(
    5,
    sizeof(SensorPacket)
  );

  dataAlert = xQueueCreate(
    1,
    sizeof(SensorPacket)
  );

  commandQueue = xQueueCreate(
    8,
    sizeof(ControlCommand)
  );

  stateQueue = xQueueCreate(
    1,
    sizeof(ActuatorReport)
  );

  if (dataQueue == nullptr ||
      dataAlert == nullptr ||
      commandQueue == nullptr ||
      stateQueue == nullptr) {

    Serial.println(
      "ERROR: Khong tao duoc FreeRTOS Queue!"
    );

    while (true) delay(1000);
  }

  // -------- CAU HINH MQTT --------
  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  mqttClient.setCallback(mqttCallback);

  mqttClient.setBufferSize(256);
  mqttClient.setKeepAlive(20);

  // -------- TAO TASK MQTT CORE 0 --------
  BaseType_t mqttCreated = xTaskCreatePinnedToCore(
    TaskMqtt,
    "Task_MQTT_WiFi",
    8192,
    nullptr,
    2,
    &taskMqttHandle,
    0
  );

  // -------- TAO TASK DOC DATA CORE 1 --------
  BaseType_t readCreated = xTaskCreatePinnedToCore(
    TaskReadSensor,
    "Task_ReadData",
    4096,
    nullptr,
    2,
    &taskReadHandle,
    1
  );

  // -------- TAO TASK CANH BAO CORE 1 --------
  BaseType_t alertCreated = xTaskCreatePinnedToCore(
    TaskAlert,
    "Task_Alert",
    4096,
    nullptr,
    3,
    &taskAlertHandle,
    1
  );

  if (mqttCreated != pdPASS ||
      readCreated != pdPASS ||
      alertCreated != pdPASS) {

    Serial.println(
      "ERROR: Khong tao duoc FreeRTOS Task!"
    );

    while (true) delay(1000);
  }

  Serial.println(
    "Da tao 3 task va 4 queue FreeRTOS."
  );

  Serial.println(
    "Task MQTT: Core 0 | Task Read: Core 1 | Task Alert: Core 1"
  );
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  // Cong viec chinh da duoc xu ly boi FreeRTOS Task
  vTaskDelay(pdMS_TO_TICKS(1000));
}
