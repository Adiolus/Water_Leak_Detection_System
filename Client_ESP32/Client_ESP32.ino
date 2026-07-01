#include <WiFi.h>
#include <esp_now.h>

// ==================== HARDWARE PINS ====================
#define FLOW_SENSOR_PIN 4

// ==================== ESP-NOW VARIABLES ====================
uint8_t masterNodeMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // REPLACE WITH YOUR MASTER MAC

volatile int pulseCount = 0;
float flowRate = 0.0;
unsigned long lastFlowCheck = 0;

typedef struct struct_message {
  float flowRate;
  int nodeID;
  unsigned long timestamp;
} struct_message;

struct_message outgoingData;

// ==================== FLOW SENSOR INTERRUPT ====================
void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

// ==================== ESP-NOW CALLBACK ====================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Optional: handle send confirmation
}

void setup() {
  Serial.begin(115200);
  
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, FALLING);
  
  WiFi.mode(WIFI_STA);
  
  // Print MAC Address for pairing
  Serial.print("Client MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }
  
  esp_now_register_send_cb(OnDataSent);
  
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, masterNodeMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    return;
  }
  
  Serial.println("Client Node Initialized");
  lastFlowCheck = millis();
}

void loop() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastFlowCheck >= 1000) {
    float seconds = (currentTime - lastFlowCheck) / 1000.0;
    flowRate = (pulseCount / 7.5) * (60.0 / seconds);
    
    pulseCount = 0;
    lastFlowCheck = currentTime;
    
    outgoingData.flowRate = flowRate;
    outgoingData.nodeID = 2;
    outgoingData.timestamp = millis();
    
    esp_now_send(masterNodeMAC, (uint8_t *)&outgoingData, sizeof(outgoingData));
    
    Serial.print("Client Flow: ");
    Serial.print(flowRate, 2);
    Serial.println(" L/min");
  }
  
  delay(10);
}
