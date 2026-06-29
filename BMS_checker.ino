#include "NimBLEDevice.h"
#include "esp_bt.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define FW_VERSION "1.0.0"
#define LED_PIN 2  // Built-in blue LED

const char* WIFI_SSID = "2.4G";
const char* WIFI_PASSWORD = "123456789";

WiFiClientSecure secureClient;

bool wifiConnected = false;
volatile bool networkBusy = false;

unsigned long lastWifiRetry = 0;
const unsigned long WIFI_RETRY_INTERVAL = 10000;  // 10 seconds

unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 10000;  // 10 seconds

// --- BATTERY MAC ADDRESSES ---
static String mac1 = "a5:c2:39:1d:e6:2e";
static String mac2 = "a5:c2:39:1d:e5:9b";

static BLEUUID serviceUUID("FF00");
static BLEUUID charUUID("FF01");

// --- CONNECTION STATES ---
static boolean doConnect1 = true, doConnect2 = true;
static boolean connected1 = false, connected2 = false;

static uint16_t retryCount1 = 0;
static uint16_t retryCount2 = 0;

static BLERemoteCharacteristic* pWriteChar1 = nullptr;
static BLERemoteCharacteristic* pWriteChar2 = nullptr;
uint8_t basicInfoCmd[] = { 0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77 };

enum TaskState { IDLE, CONNECT_1, POLL_1, CONNECT_2, POLL_2, SEND_API };
TaskState currentState = IDLE;
unsigned long stateTimer = 0;
bool dataReceived = false; // Flag to know when polling is done

// --- DUAL BUFFERS & STATE TRACKERS ---
uint8_t buffer1[64];
int idx1 = 0;
uint8_t buffer2[64];
int idx2 = 0;

// Event Tracking (To prevent serial spam)
int lastSoC1 = -1, lastSoC2 = -1;
float lastCurrent1 = -999.0, lastCurrent2 = -999.0;
volatile uint8_t batteryUpdateMask = 0;

#define BAT1_UPDATED 0x01
#define BAT2_UPDATED 0x02

NimBLEClient* pClient1 = nullptr;
NimBLEClient* pClient2 = nullptr;

// Live System Data
int soc1 = -1, soc2 = -1;
float voltage1 = 0.0, voltage2 = 0.0;
float current1 = 0.0, current2 = 0.0;

struct SystemData {
  int avgSoC = -1;
  float minVoltage = 0;
  float netCurrent = 0;
  float totalAbsCurrent = 0;
};

SystemData systemData;

enum SystemState {
  SYSTEM_UNKNOWN,
  SYSTEM_BATTERY,
  SYSTEM_GRID
};

SystemState currentState = SYSTEM_UNKNOWN;
SystemState previousState = SYSTEM_UNKNOWN;

enum EventReason {
  REASON_NONE,
  REASON_LOW_SOC,
  REASON_LOW_VOLTAGE,
  REASON_HIGH_CURRENT
};

EventReason lastReason = REASON_NONE;

struct ControllerConfig {
  float voltageCutoff = 24.0;
  float voltageRecover = 25.5;

  float maxCurrent = 100;

  int socOn = 50;
  int socOff = 40;
};

ControllerConfig config;

// ==========================================
// SYSTEM LOGIC & SAFETY THRESHOLDS
// ==========================================
bool processBatteryData() {
  if (soc1 < 0 || soc2 < 0)
    return false;

  systemData.avgSoC = (soc1 + soc2) / 2;

  systemData.minVoltage = min(voltage1, voltage2);

  systemData.netCurrent = current1 + current2;
  systemData.totalAbsCurrent = fabs(current1) + fabs(current2);

  return true;
}

void evaluateSystemState() {
  SystemState newState = currentState;

  if (systemData.avgSoC <= config.socOff) {
    newState = SYSTEM_GRID;
    lastReason = REASON_LOW_SOC;
  } else if (systemData.minVoltage <= config.voltageCutoff) {
    newState = SYSTEM_GRID;
    lastReason = REASON_LOW_VOLTAGE;
  } else if (systemData.totalAbsCurrent > config.maxCurrent) {
    newState = SYSTEM_GRID;
    lastReason = REASON_HIGH_CURRENT;
  } else if (systemData.avgSoC >= config.socOn && systemData.minVoltage >= config.voltageRecover) {
    newState = SYSTEM_BATTERY;
    lastReason = REASON_NONE;
  }

  previousState = currentState;
  currentState = newState;
}

void processEvents() {
  Serial.println();
  Serial.println("========== SYSTEM ==========");

  Serial.printf("Battery 1 : %d%%  %.2fV  %.2fA\n",
                soc1,
                voltage1,
                current1);

  Serial.printf("Battery 2 : %d%%  %.2fV  %.2fA\n",
                soc2,
                voltage2,
                current2);

  Serial.println("----------------------------");

  Serial.printf("Average SoC  : %d%%\n",
                systemData.avgSoC);

  Serial.printf("Minimum Volt : %.2fV\n",
                systemData.minVoltage);

  Serial.printf("Total Current: %.2fA\n",
                systemData.netCurrent);

  Serial.println("----------------------------");

  if (currentState == SYSTEM_GRID)
    Serial.println("STATE : GRID");

  if (currentState == SYSTEM_BATTERY)
    Serial.println("STATE : BATTERY");

  if (currentState != previousState) {
    Serial.println("*** STATE CHANGED ***");

    Serial.print("Reason : ");

    switch (lastReason) {
      case REASON_LOW_SOC:
        Serial.println("LOW_SOC");
        break;

      case REASON_LOW_VOLTAGE:
        Serial.println("LOW_VOLTAGE");
        break;

      case REASON_HIGH_CURRENT:
        Serial.println("HIGH_CURRENT");
        break;

      default:
        Serial.println("NONE");
    }

    networkBusy = true;
    sendToGoogleSheet();
    networkBusy = false;

    // Future:
    // switchRelay();
    // sendApiEvent();
    // blinkLED();
  }

  Serial.println("============================");
}

// ==========================================
// DATA PARSER & ROUTER
// ==========================================
static void notifyCallback(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  if (!pBLERemoteCharacteristic) return;

  // FIX 1: Add 'const' to match NimBLE's return type
  const NimBLERemoteService* pSvc = pBLERemoteCharacteristic->getRemoteService();
  if (!pSvc) return;

  const NimBLEClient* pClient = pSvc->getClient();
  if (!pClient) return;

  // FIX 2: Convert std::string (NimBLE) to Arduino String (Your code)
  // This solves the 'conflicting declaration' and 'no match for operator==' errors
  String senderMac = String(pClient->getPeerAddress().toString().c_str());

  // Assign pointers based on sender
  uint8_t* targetBuffer;
  int* targetIdx;
  int* targetLastSoC;
  float* targetLastCurrent;
  int* targetSoC;
  float* targetVoltage;
  float* targetCurrent;
  int batNum;

  if (senderMac == mac1) {
    targetBuffer = buffer1;
    targetIdx = &idx1;
    targetLastSoC = &lastSoC1;
    targetLastCurrent = &lastCurrent1;
    targetSoC = &soc1;
    targetVoltage = &voltage1;
    targetCurrent = &current1;
    batNum = 1;
  } else if (senderMac == mac2) {
    targetBuffer = buffer2;
    targetIdx = &idx2;
    targetLastSoC = &lastSoC2;
    targetLastCurrent = &lastCurrent2;
    targetSoC = &soc2;
    targetVoltage = &voltage2;
    targetCurrent = &current2;
    batNum = 2;
  } else {
    return;
  }

  // Add to buffer
  for (int i = 0; i < length; i++) {
    if (*targetIdx < 64) targetBuffer[(*targetIdx)++] = pData[i];
  }

  // Process when footer (0x77) arrives
  if (pData[length - 1] == 0x77) {
    if (*targetIdx > 23 && targetBuffer[0] == 0xDD && targetBuffer[1] == 0x03) {

      // Extract Live Data
      *targetVoltage = ((targetBuffer[4] << 8) | targetBuffer[5]) * 0.01;
      int16_t rawCurrent = (targetBuffer[6] << 8) | targetBuffer[7];
      *targetCurrent = rawCurrent * 0.01;
      *targetSoC = targetBuffer[23];

      bool stateChanged = false;

      // Check for changes to prevent serial spam
      if (*targetSoC != *targetLastSoC) {
        stateChanged = true;
        *targetLastSoC = *targetSoC;
      }
      if (abs(*targetCurrent - *targetLastCurrent) > 0.2) {
        stateChanged = true;
        *targetLastCurrent = *targetCurrent;
      }

      if (stateChanged) {
        if (batNum == 1)
          batteryUpdateMask |= BAT1_UPDATED;
        else
          batteryUpdateMask |= BAT2_UPDATED;
      }
    }
    *targetIdx = 0;
    memset(targetBuffer, 0, 64);
  }
}

// ==========================================
// CONNECTION MANAGEMENT (Remains the same)
// ==========================================

class ClientCB1 : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pclient) {
    connected1 = true;
    Serial.println("Bat 1 Connected");
  }
  void onDisconnect(NimBLEClient* pclient) {
    connected1 = false;
    Serial.println("Bat 1 Disconnected");
  }
};

class ClientCB2 : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pclient) {
    connected2 = true;
    Serial.println("Bat 2 Connected");
  }
  void onDisconnect(NimBLEClient* pclient) {
    connected2 = false;
    Serial.println("Bat 2 Disconnected");
  }
};

bool connectToBMS(String mac, bool isBat1) {
  logInfo("Connecting to Battery " + String(isBat1 ? 1 : 2) + " (" + mac + ")...");

  // 1. Reuse the client or create it only once
  NimBLEClient* pClient = (isBat1) ? pClient1 : pClient2;

  if (pClient == nullptr) {
    pClient = NimBLEDevice::createClient();
    if (isBat1) pClient1 = pClient;
    else pClient2 = pClient;

    // Set callbacks only when the client is first created
    if (isBat1) pClient->setClientCallbacks(new ClientCB1());
    else pClient->setClientCallbacks(new ClientCB2());
  }

  NimBLEAddress addr(mac.c_str(), BLE_ADDR_PUBLIC);

  if (!pClient->connect(addr)) {
    logError("Battery " + String(isBat1 ? 1 : 2) + ": Connection failed.");
    return false;
  }

  logInfo("✓ Connected.");

  NimBLERemoteService* pRemoteService = pClient->getService(serviceUUID);

  if (pRemoteService == nullptr) {
    logError("❌ FF00 service not found.");
    pClient->disconnect();
    return false;
  }

  logInfo("✓ FF00 service found.");

  NimBLERemoteCharacteristic* pNotifyChar = pRemoteService->getCharacteristic(charUUID);

  if (pNotifyChar == nullptr) {
    logError("❌ FF01 notify characteristic not found.");
    pClient->disconnect();
    return false;
  }

  logInfo("✓ FF01 notify characteristic found.");

  if (pNotifyChar->canNotify()) {
    pNotifyChar->subscribe(true, notifyCallback, false);
  }

  NimBLERemoteCharacteristic* pWrite = pRemoteService->getCharacteristic("FF02");
  if (pWrite == nullptr) {
    logError("❌ FF02 write characteristic not found.");
    pClient->disconnect();
    return false;
  }

  logInfo("✓ FF02 write characteristic found.");

  delay(500);

  pWrite->writeValue(basicInfoCmd, sizeof(basicInfoCmd), true);

  if (isBat1) pWriteChar1 = pWrite;
  else pWriteChar2 = pWrite;

  logInfo("Battery " + String(isBat1 ? 1 : 2) + " ready.");
  return true;
}

void pollBatteries() {
  if (networkBusy)
    return;
  batteryUpdateMask = 0;

  if (connected1 && pWriteChar1) pWriteChar1->writeValue(basicInfoCmd, sizeof(basicInfoCmd), true);
  if (connected2 && pWriteChar2) pWriteChar2->writeValue(basicInfoCmd, sizeof(basicInfoCmd), true);

  blinkLED(1, 100);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  NimBLEDevice::init("BMS_Controller");
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  connectWifi();
  WiFi.setSleep(false);
  WiFi.onEvent(WiFiEvent);
  secureClient.setInsecure();
}

void loop() {
  // 0. WiFi Check & Reconnection
  checkWifi();

  // 1. Critical: Prevent BLE operations if WiFi is busy
  if (networkBusy) {
    return;  // Stop the loop, let the WiFi finish its request
  }

  // 2. Battery Data Processing
  if (batteryUpdateMask == (BAT1_UPDATED | BAT2_UPDATED)) {
    batteryUpdateMask = 0;

    if (processBatteryData()) {
      evaluateSystemState();
      processEvents();
    }
  }

  // 3. Command Processing
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    processCommand(command);
  }

  // 4. Connection Logic (with safety)
  if (doConnect1) {
    if (connectToBMS(mac1, true)) {
      retryCount1 = 0;
      logInfo("Bat 1 Connected!");
    }
    doConnect1 = false;
    delay(1000);
  }

  if (doConnect2) {
    if (connectToBMS(mac2, false)) {
      retryCount2 = 0;
      logInfo("Bat 2 Connected!");
    }
    doConnect2 = false;
    delay(1000);
  }

  // 5. Automatic Polling (With 'networkBusy' check)
  if (connected1 && connected2) {
    if (millis() - lastPollTime >= POLL_INTERVAL) {
      pollBatteries();
      lastPollTime = millis();
    }
  }

  // 6. Hardened Retry Logic (Prevents infinite loop crash)
  static unsigned long lastRetryCheck = 0;
  if (millis() - lastRetryCheck > 5000) {  // Only check every 5 seconds
    if (!connected1 && !doConnect1) {
      retryCount1++;
      logWarning("Battery 1 disconnected. Retry #" + String(retryCount1));
      doConnect1 = true;
    }
    if (!connected2 && !doConnect2) {
      retryCount2++;
      logWarning("Battery 2 disconnected. Retry #" + String(retryCount2));
      doConnect2 = true;
    }
    lastRetryCheck = millis();
  }
}