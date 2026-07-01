#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// ==================== HARDWARE PINS ====================
#define FLOW_SENSOR_PIN 4
#define RELAY_PIN 5
#define LED_GREEN 12
#define LED_YELLOW 13
#define LED_RED 14

// ==================== OLED SETUP ====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==================== RTC SETUP ====================
RTC_DS3231 rtc;

// ==================== EEPROM CONFIGURATION ====================
#define EEPROM_SIZE 2048
#define LEARNING_DAYS 30
#define HOURS_PER_DAY 24

// ==================== FSM STATES ====================
enum SystemState {
  IDLE,
  NORMAL_FLOW,
  CONTINUOUS_FLOW,
  OVERUSAGE,
  MINI_LEAK,
  NON_USAGE_TIME_LEAK,
  CRITICAL_LEAK,
  CUTOFF
};

SystemState currentState = IDLE;

// ==================== ML LEARNING STRUCTURE ====================
struct HourlyPattern {
  float avgFlow;
  float stdDev;
  uint16_t sampleCount;
  float totalFlow;
  float sumSquares;
};

HourlyPattern hourlyPatterns[HOURS_PER_DAY];
bool learningMode = true;
int learningDaysCompleted = 0;

#define ADDR_LEARNING_MODE 0
#define ADDR_DAYS_COMPLETED 4
#define ADDR_PATTERNS_START 8

// ==================== FLOW SENSOR VARIABLES ====================
volatile int pulseCount = 0;
float flowRate = 0.0;
float totalVolume = 0.0;
unsigned long lastFlowCheck = 0;
const int FLOW_CHECK_INTERVAL = 1000;

// ==================== FSM TIMING VARIABLES ====================
unsigned long stateEntryTime = 0;
unsigned long continuousFlowStartTime = 0;
unsigned long overusageStartTime = 0;
unsigned long miniLeakStartTime = 0;
unsigned long nonUsageLeakStartTime = 0;
unsigned long criticalLeakStartTime = 0;

int lastHourRecorded = -1;
float hourlyFlowAccumulator = 0.0;
int hourlyReadingsCount = 0;

// ==================== FLOW THRESHOLDS ====================
const float IDLE_THRESHOLD = 0.1;
const float MINI_LEAK_THRESHOLD = 0.5;
const float NON_USAGE_THRESHOLD = 0.3;
const float CRITICAL_LEAK_THRESHOLD = 10.0;

// ==================== TIMING CONSTRAINTS ====================
const unsigned long OVERUSAGE_DURATION = 30 * 60 * 1000;
const unsigned long MINI_LEAK_DURATION = 10 * 60 * 1000;
const unsigned long NON_USAGE_LEAK_DURATION = 2 * 60 * 1000;
const unsigned long CRITICAL_LEAK_DURATION = 1 * 60 * 1000;
const unsigned long CONTINUOUS_FLOW_TIMEOUT = 15 * 60 * 1000;

// ==================== ML PARAMETERS ====================
const float ANOMALY_THRESHOLD = 2.5;
const float MIN_SAMPLES_FOR_LEARNING = 5;

// ==================== ESP-NOW VARIABLES ====================
uint8_t clientNodeMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // REPLACE WITH YOUR CLIENT MAC
float clientFlowRate = 0.0;

typedef struct struct_message {
  float flowRate;
  int nodeID;
  unsigned long timestamp;
} struct_message;

struct_message outgoingData;
struct_message incomingData;

// ==================== FLOW SENSOR INTERRUPT ====================
void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

// ==================== ESP-NOW CALLBACK ====================
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  memcpy(&incomingData, data, sizeof(incomingData));
  if (incomingData.nodeID == 2) {
    clientFlowRate = incomingData.flowRate;
  }
}

// ==================== EEPROM FUNCTIONS ====================
void saveToEEPROM() {
  EEPROM.write(ADDR_LEARNING_MODE, learningMode ? 1 : 0);
  EEPROM.write(ADDR_DAYS_COMPLETED, learningDaysCompleted);
  
  int addr = ADDR_PATTERNS_START;
  for (int i = 0; i < HOURS_PER_DAY; i++) {
    EEPROM.put(addr, hourlyPatterns[i]);
    addr += sizeof(HourlyPattern);
  }
  EEPROM.commit();
  Serial.println("Saved learning data to EEPROM");
}

void loadFromEEPROM() {
  learningMode = EEPROM.read(ADDR_LEARNING_MODE) == 1;
  learningDaysCompleted = EEPROM.read(ADDR_DAYS_COMPLETED);
  
  int addr = ADDR_PATTERNS_START;
  for (int i = 0; i < HOURS_PER_DAY; i++) {
    EEPROM.get(addr, hourlyPatterns[i]);
    addr += sizeof(HourlyPattern);
  }
  
  Serial.print("Loaded from EEPROM. Learning mode: ");
  Serial.print(learningMode ? "ON" : "OFF");
  Serial.print(" | Days completed: ");
  Serial.println(learningDaysCompleted);
}

void initializeLearning() {
  for (int i = 0; i < HOURS_PER_DAY; i++) {
    hourlyPatterns[i].avgFlow = 0.0;
    hourlyPatterns[i].stdDev = 0.0;
    hourlyPatterns[i].sampleCount = 0;
    hourlyPatterns[i].totalFlow = 0.0;
    hourlyPatterns[i].sumSquares = 0.0;
  }
  learningMode = true;
  learningDaysCompleted = 0;
  saveToEEPROM();
}

// ==================== ML LEARNING FUNCTIONS ====================
void updateHourlyPattern(int hour, float avgFlowThisHour) {
  HourlyPattern *pattern = &hourlyPatterns[hour];
  
  pattern->sampleCount++;
  pattern->totalFlow += avgFlowThisHour;
  pattern->sumSquares += (avgFlowThisHour * avgFlowThisHour);
  
  pattern->avgFlow = pattern->totalFlow / pattern->sampleCount;
  
  if (pattern->sampleCount > 1) {
    float variance = (pattern->sumSquares / pattern->sampleCount) - 
                     (pattern->avgFlow * pattern->avgFlow);
    pattern->stdDev = sqrt(max(0.0f, variance));
  }
  
  Serial.print("Hour ");
  Serial.print(hour);
  Serial.print(" updated: Avg=");
  Serial.print(pattern->avgFlow, 3);
  Serial.print(" StdDev=");
  Serial.print(pattern->stdDev, 3);
  Serial.print(" Samples=");
  Serial.println(pattern->sampleCount);
}

void processHourlyData() {
  DateTime now = rtc.now();
  int currentHour = now.hour();
  
  if (currentHour != lastHourRecorded && lastHourRecorded != -1) {
    float avgFlowLastHour = (hourlyReadingsCount > 0) ? 
                            (hourlyFlowAccumulator / hourlyReadingsCount) : 0.0;
    
    updateHourlyPattern(lastHourRecorded, avgFlowLastHour);
    
    hourlyFlowAccumulator = 0.0;
    hourlyReadingsCount = 0;
    
    if (currentHour == 0 && learningMode) {
      learningDaysCompleted++;
      Serial.print("Learning day completed: ");
      Serial.print(learningDaysCompleted);
      Serial.print("/");
      Serial.println(LEARNING_DAYS);
      
      if (learningDaysCompleted >= LEARNING_DAYS) {
        learningMode = false;
        Serial.println("*** LEARNING COMPLETE - ML MODEL ACTIVE ***");
      }
      saveToEEPROM();
    }
  }
  
  hourlyFlowAccumulator += flowRate;
  hourlyReadingsCount++;
  lastHourRecorded = currentHour;
}

bool isNonUsageTime() {
  if (learningMode) {
    return false;
  }
  
  DateTime now = rtc.now();
  int currentHour = now.hour();
  HourlyPattern pattern = hourlyPatterns[currentHour];
  
  if (pattern.sampleCount < MIN_SAMPLES_FOR_LEARNING) {
    return false;
  }
  
  bool isNonUsage = (pattern.avgFlow < 0.2 && pattern.stdDev < 0.15);
  
  return isNonUsage;
}

bool isAnomalousFlow(float currentFlow) {
  if (learningMode) {
    return false;
  }
  
  DateTime now = rtc.now();
  int currentHour = now.hour();
  HourlyPattern pattern = hourlyPatterns[currentHour];
  
  if (pattern.sampleCount < MIN_SAMPLES_FOR_LEARNING) {
    return false;
  }
  
  if (pattern.stdDev == 0) {
    return abs(currentFlow - pattern.avgFlow) > 0.5;
  }
  
  float zScore = (currentFlow - pattern.avgFlow) / pattern.stdDev;
  
  return (zScore > ANOMALY_THRESHOLD);
}

void printLearningStatus() {
  Serial.println("\n===== LEARNING STATUS =====");
  Serial.print("Mode: ");
  Serial.println(learningMode ? "LEARNING" : "OPERATIONAL");
  Serial.print("Days completed: ");
  Serial.print(learningDaysCompleted);
  Serial.print("/");
  Serial.println(LEARNING_DAYS);
  Serial.println("\nHourly Patterns:");
  
  for (int h = 0; h < HOURS_PER_DAY; h++) {
    HourlyPattern p = hourlyPatterns[h];
    if (p.sampleCount > 0) {
      Serial.print("Hour ");
      if (h < 10) Serial.print("0");
      Serial.print(h);
      Serial.print(": Avg=");
      Serial.print(p.avgFlow, 3);
      Serial.print(" StdDev=");
      Serial.print(p.stdDev, 3);
      Serial.print(" Samples=");
      Serial.print(p.sampleCount);
      Serial.print(" [");
      Serial.print(p.avgFlow < 0.2 && p.stdDev < 0.15 ? "NON-USAGE" : "USAGE");
      Serial.println("]");
    }
  }
  Serial.println("===========================\n");
}

// ==================== HELPER FUNCTIONS ====================
String getStateName(SystemState state) {
  switch(state) {
    case IDLE: return "IDLE";
    case NORMAL_FLOW: return "NORMAL_FLOW";
    case CONTINUOUS_FLOW: return "CONTINUOUS_FLOW";
    case OVERUSAGE: return "OVERUSAGE";
    case MINI_LEAK: return "MINI_LEAK";
    case NON_USAGE_TIME_LEAK: return "NON_USAGE_LEAK";
    case CRITICAL_LEAK: return "CRITICAL_LEAK";
    case CUTOFF: return "CUTOFF";
    default: return "UNKNOWN";
  }
}

void updateLEDs() {
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);
  
  switch(currentState) {
    case IDLE:
    case NORMAL_FLOW:
      digitalWrite(LED_GREEN, HIGH);
      break;
    case CONTINUOUS_FLOW:
    case OVERUSAGE:
    case MINI_LEAK:
      digitalWrite(LED_YELLOW, HIGH);
      break;
    case NON_USAGE_TIME_LEAK:
    case CRITICAL_LEAK:
    case CUTOFF:
      digitalWrite(LED_RED, HIGH);
      break;
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("AQUAGUARD ML");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  
  display.setCursor(0, 15);
  display.print("Flow: ");
  display.print(flowRate, 2);
  display.println(" L/min");
  
  display.setCursor(0, 27);
  display.print("State: ");
  String stateName = getStateName(currentState);
  if (stateName.length() > 10) {
    display.println(stateName.substring(0, 10));
  } else {
    display.println(stateName);
  }
  
  display.setCursor(0, 39);
  if (learningMode) {
    display.print("Learning: ");
    display.print(learningDaysCompleted);
    display.print("/");
    display.println(LEARNING_DAYS);
  } else {
    DateTime now = rtc.now();
    display.print(now.hour());
    display.print(":");
    if (now.minute() < 10) display.print("0");
    display.print(now.minute());
    if (isNonUsageTime()) {
      display.print(" [NU]");
    }
  }
  
  display.setCursor(0, 51);
  display.print("Valve: ");
  display.println(currentState == CUTOFF ? "CLOSED" : "OPEN");
  
  display.display();
}

void activateCutoff() {
  digitalWrite(RELAY_PIN, HIGH);
  Serial.println("!!! EMERGENCY CUTOFF ACTIVATED !!!");
}

void deactivateCutoff() {
  digitalWrite(RELAY_PIN, LOW);
  Serial.println("Valve reopened - System reset");
}

// ==================== FSM LOGIC ====================
void updateFSM() {
  unsigned long currentTime = millis();
  SystemState previousState = currentState;
  
  float flowDifference = abs(flowRate - clientFlowRate);
  bool massBalanceViolation = (flowRate > 1.0 && clientFlowRate < 0.5 && flowDifference > 0.8);
  
  bool isAnomaly = isAnomalousFlow(flowRate);
  
  switch(currentState) {
    
    case IDLE:
      if (flowRate > IDLE_THRESHOLD) {
        currentState = NORMAL_FLOW;
        stateEntryTime = currentTime;
      }
      break;
    
    case NORMAL_FLOW:
      if (flowRate < IDLE_THRESHOLD) {
        currentState = IDLE;
      }
      else if (flowRate > CRITICAL_LEAK_THRESHOLD) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      else if (isNonUsageTime() && flowRate > NON_USAGE_THRESHOLD) {
        currentState = NON_USAGE_TIME_LEAK;
        nonUsageLeakStartTime = currentTime;
      }
      else if (flowRate > MINI_LEAK_THRESHOLD) {
        currentState = MINI_LEAK;
        miniLeakStartTime = currentTime;
      }
      else if (currentTime - stateEntryTime > 5000) {
        currentState = CONTINUOUS_FLOW;
        continuousFlowStartTime = currentTime;
      }
      else if (massBalanceViolation) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      break;
    
    case CONTINUOUS_FLOW:
      if (flowRate < IDLE_THRESHOLD) {
        currentState = IDLE;
      }
      else if (flowRate > CRITICAL_LEAK_THRESHOLD) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      else if (flowRate > MINI_LEAK_THRESHOLD) {
        currentState = MINI_LEAK;
        miniLeakStartTime = currentTime;
      }
      else if (isNonUsageTime() && flowRate > NON_USAGE_THRESHOLD) {
        currentState = NON_USAGE_TIME_LEAK;
        nonUsageLeakStartTime = currentTime;
      }
      else if (currentTime - continuousFlowStartTime > OVERUSAGE_DURATION) {
        currentState = OVERUSAGE;
        overusageStartTime = currentTime;
      }
      else if (massBalanceViolation) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      break;
    
    case OVERUSAGE:
      if (flowRate < IDLE_THRESHOLD) {
        currentState = IDLE;
      }
      else if (flowRate > CRITICAL_LEAK_THRESHOLD) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      else if (flowRate > MINI_LEAK_THRESHOLD) {
        currentState = MINI_LEAK;
        miniLeakStartTime = currentTime;
      }
      else if (isNonUsageTime() && flowRate > NON_USAGE_THRESHOLD) {
        currentState = NON_USAGE_TIME_LEAK;
        nonUsageLeakStartTime = currentTime;
      }
      else if (massBalanceViolation) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      break;
    
    case MINI_LEAK:
      if (flowRate < IDLE_THRESHOLD) {
        currentState = IDLE;
      }
      else if (flowRate > CRITICAL_LEAK_THRESHOLD) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      else if (flowRate > MINI_LEAK_THRESHOLD && 
               (currentTime - miniLeakStartTime) > MINI_LEAK_DURATION) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      else if (flowRate <= MINI_LEAK_THRESHOLD && flowRate > IDLE_THRESHOLD) {
        currentState = CONTINUOUS_FLOW;
        continuousFlowStartTime = currentTime;
      }
      else if (massBalanceViolation) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      break;
    
    case NON_USAGE_TIME_LEAK:
      if (flowRate < IDLE_THRESHOLD) {
        currentState = IDLE;
      }
      else if (flowRate > CRITICAL_LEAK_THRESHOLD) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      else if (flowRate > NON_USAGE_THRESHOLD && 
               (currentTime - nonUsageLeakStartTime) > NON_USAGE_LEAK_DURATION) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      else if (!isNonUsageTime()) {
        if (flowRate > MINI_LEAK_THRESHOLD) {
          currentState = MINI_LEAK;
          miniLeakStartTime = currentTime;
        } else if (flowRate > IDLE_THRESHOLD) {
          currentState = CONTINUOUS_FLOW;
          continuousFlowStartTime = currentTime;
        }
      }
      else if (massBalanceViolation) {
        currentState = CRITICAL_LEAK;
        criticalLeakStartTime = currentTime;
      }
      break;
    
    case CRITICAL_LEAK:
      if (flowRate < IDLE_THRESHOLD && clientFlowRate < IDLE_THRESHOLD) {
        currentState = IDLE;
      }
      else if (massBalanceViolation || 
               (currentTime - criticalLeakStartTime) > CRITICAL_LEAK_DURATION) {
        currentState = CUTOFF;
        activateCutoff();
      }
      break;
    
    case CUTOFF:
      break;
  }
  
  if (currentState != previousState) {
    Serial.print("STATE CHANGE: ");
    Serial.print(getStateName(previousState));
    Serial.print(" -> ");
    Serial.print(getStateName(currentState));
    
    if (currentState == NON_USAGE_TIME_LEAK) {
      Serial.print(" [ML detected non-usage hour]");
    }
    if (isAnomaly && !learningMode) {
      Serial.print(" [ML flagged as anomaly]");
    }
    Serial.println();
    
    sendStateUpdate();
  }
}

// ==================== CALCULATE FLOW RATE ====================
void calculateFlowRate() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastFlowCheck >= FLOW_CHECK_INTERVAL) {
    float seconds = (currentTime - lastFlowCheck) / 1000.0;
    flowRate = (pulseCount / 7.5) * (60.0 / seconds);
    
    totalVolume += (flowRate / 60.0) * seconds;
    
    pulseCount = 0;
    lastFlowCheck = currentTime;
    
    processHourlyData();
    updateFSM();
    updateLEDs();
    updateDisplay();
    sendFlowData();
    printDebugData();
  }
}

void sendFlowData() {
  outgoingData.flowRate = flowRate;
  outgoingData.nodeID = 1;
  outgoingData.timestamp = millis();
  
  esp_now_send(clientNodeMAC, (uint8_t *)&outgoingData, sizeof(outgoingData));
}

void sendStateUpdate() {
  Serial.print("{\"node\":1,\"state\":\"");
  Serial.print(getStateName(currentState));
  Serial.print("\",\"flow\":");
  Serial.print(flowRate);
  Serial.print(",\"clientFlow\":");
  Serial.print(clientFlowRate);
  Serial.print(",\"learningMode\":");
  Serial.print(learningMode ? "true" : "false");
  Serial.print(",\"learningDays\":");
  Serial.print(learningDaysCompleted);
  Serial.print(",\"timestamp\":");
  Serial.print(millis());
  Serial.println("}");
}

void printDebugData() {
  DateTime now = rtc.now();
  
  Serial.print("Time: ");
  if (now.hour() < 10) Serial.print("0");
  Serial.print(now.hour());
  Serial.print(":");
  if (now.minute() < 10) Serial.print("0");
  Serial.print(now.minute());
  Serial.print(" | Flow: ");
  Serial.print(flowRate, 2);
  Serial.print(" L/min | Client: ");
  Serial.print(clientFlowRate, 2);
  Serial.print(" L/min | State: ");
  Serial.print(getStateName(currentState));
  
  if (!learningMode) {
    Serial.print(" | NonUsage: ");
    Serial.print(isNonUsageTime() ? "YES" : "NO");
    Serial.print(" | Anomaly: ");
    Serial.print(isAnomalousFlow(flowRate) ? "YES" : "NO");
  } else {
    Serial.print(" | Learning: Day ");
    Serial.print(learningDaysCompleted);
  }
  Serial.println();
}

void handleSerialCommand() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "RESET") {
      currentState = IDLE;
      deactivateCutoff();
      Serial.println("System reset to IDLE");
    }
    else if (cmd == "CUTOFF") {
      currentState = CUTOFF;
      activateCutoff();
      Serial.println("Manual cutoff triggered");
    }
    else if (cmd == "STATUS") {
      printLearningStatus();
    }
    else if (cmd == "RESET_LEARNING") {
      Serial.println("Resetting learning data...");
      initializeLearning();
      Serial.println("Learning reset complete");
    }
    else if (cmd == "SAVE") {
      saveToEEPROM();
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  EEPROM.begin(EEPROM_SIZE);
  
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  
  digitalWrite(RELAY_PIN, LOW);
  
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, FALLING);
  
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    while (1);
  }
  
  // Uncomment to set time once
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found!");
    while (1);
  }
  display.clearDisplay();
  display.display();
  
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, clientNodeMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    return;
  }
  
  loadFromEEPROM();
  
  Serial.println("\n================================");
  Serial.println("AQUAGUARD ML - MASTER NODE");
  Serial.println("================================");
  Serial.println("States: IDLE -> NORMAL -> CONTINUOUS -> OVERUSAGE/MINI/NON_USAGE -> CRITICAL -> CUTOFF");
  Serial.println("\nSerial Commands:");
  Serial.println("  STATUS - Show learning status");
  Serial.println("  RESET - Reset system to IDLE");
  Serial.println("  CUTOFF - Manual cutoff");
  Serial.println("  RESET_LEARNING - Clear learning data");
  Serial.println("  SAVE - Save learning data to EEPROM");
  Serial.println("================================\n");
  
  printLearningStatus();
  
  lastFlowCheck = millis();
}

void loop() {
  calculateFlowRate();
  handleSerialCommand();
  
  static unsigned long lastSave = 0;
  if (millis() - lastSave > 3600000) {
    saveToEEPROM();
    lastSave = millis();
  }
  
  delay(10);
}
