# AquaGuard ML - Water Leak Detection System

An intelligent water leak detection system using edge ML on ESP32 microcontrollers with multi-zone anomaly detection.

## 📦 Project Structure

```
aquaguard_project/
├── Master_ESP32/
│   └── Master_ESP32.ino          # Main ESP32 code with ML and FSM
├── Client_ESP32/
│   └── Client_ESP32.ino          # Secondary ESP32 for flow comparison
├── serial_bridge/
│   ├── bridge.js                 # Serial to WebSocket bridge
│   └── package.json              # Node.js dependencies
├── web_integration/
│   └── index.html                # Web dashboard example
└── README.md                     # This file
```

## 🚀 Quick Start

### Step 1: Upload Arduino Code

1. **Install Required Libraries** (Arduino IDE → Sketch → Include Library → Manage Libraries):
   - `ESP32` board support (via Board Manager)
   - `RTClib` by Adafruit
   - `Adafruit SSD1306`
   - `Adafruit GFX Library`

2. **Configure MAC Addresses**:
   - Upload `Client_ESP32.ino` first and note its MAC address from Serial Monitor
   - Update `Master_ESP32.ino` line 60 with the client MAC:
     ```cpp
     uint8_t clientNodeMAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
     ```
   - Upload `Master_ESP32.ino` and note its MAC address
   - Update `Client_ESP32.ino` line 8 with the master MAC:
     ```cpp
     uint8_t masterNodeMAC[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
     ```

3. **Set RTC Time** (one-time setup):
   - Uncomment line 466 in `Master_ESP32.ino`:
     ```cpp
     rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
     ```
   - Upload, then comment out and re-upload

### Step 2: Hardware Connections

**Master Node:**
- Flow Sensor → Pin 4
- Relay (solenoid) → Pin 5
- Green LED → Pin 12
- Yellow LED → Pin 13
- Red LED → Pin 14
- RTC (I2C) → SDA/SCL
- OLED (I2C) → SDA/SCL

**Client Node:**
- Flow Sensor → Pin 4

### Step 3: Serial Bridge Setup

1. Install [Node.js](https://nodejs.org/) if not already installed
2. Navigate to `serial_bridge/` and run:
   ```bash
   npm install
   npm start
   ```
3. Edit `bridge.js` line 8 to configure your serial port:
   - **Windows**: `COM3` (check Device Manager)
   - **Linux**: `/dev/ttyUSB0`
   - **macOS**: `/dev/cu.usbserial-XXXX`

### Step 4: Web Integration

Open `web_integration/index.html` in a browser, or integrate the bridge into your existing site:

```javascript
const ws = new WebSocket('ws://localhost:3001');

ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.type === 'state_update') {
    console.log('Flow:', message.data.flow);
    console.log('State:', message.data.state);
    console.log('Learning Mode:', message.data.learningMode);
  }
};

function sendCommand(cmd) {
  ws.send(JSON.stringify({ action: cmd }));
}
```

## 🧠 ML System Overview

The system learns normal water usage patterns over 30 days, then detects anomalies using statistical analysis:

### Data Collection (Days 1-30)
- Hourly flow rate averaging and standard deviation calculation
- Data persisted in EEPROM (survives power loss)

### Anomaly Detection (After Day 30)
- Each hour has a learned usage profile (e.g., 8 AM avg = 4.2 L/min, σ = 1.5)
- Uses Z-score analysis: `Z = (current - average) / std_dev`
- Flow > 2.5σ from mean = anomalous
- Detects non-usage hour anomalies (avg < 0.2 L/min with low variance)

### Key Features
- **Edge computing** - No cloud dependency
- **Adaptive** - Learns building-specific patterns
- **Resilient** - Survives power loss via EEPROM
- **Lightweight** - Runs on microcontroller
- **Self-calibrating** - No manual threshold configuration

## 📊 FSM States

| State | Trigger | LED | Action |
|-------|---------|-----|--------|
| IDLE | Flow < 0.1 L/min | Green | Normal |
| NORMAL_FLOW | 0.1–0.5 L/min | Green | Normal |
| CONTINUOUS_FLOW | >5 sec sustained flow | Green | Normal |
| OVERUSAGE | >30 min flow | Yellow | Alert |
| MINI_LEAK | >0.5 L/min for 10 min | Yellow | Warning |
| NON_USAGE_TIME_LEAK | >0.3 L/min for 2 min during non-usage hours | Red | Alert |
| CRITICAL_LEAK | >10 L/min for 1 min or mass-balance violation | Red | Alert |
| CUTOFF | Critical leak persists | Red (flashing) | **Valve closes** |

## 🔧 Serial Commands

Open Serial Monitor (115200 baud) and send:

- `STATUS` - Show learning progress and hourly patterns
- `RESET` - Return to IDLE state and reopen valve
- `CUTOFF` - Manually trigger emergency cutoff
- `RESET_LEARNING` - Clear data and restart 30-day cycle
- `SAVE` - Manually persist learning data to EEPROM

## 🐛 Troubleshooting

| Issue | Solution |
|-------|----------|
| ESP32 won't upload | Hold BOOT button while uploading; verify "ESP32 Dev Module" is selected |
| Bridge can't find serial port | Check `SERIAL_PORT` in `bridge.js` matches Device Manager / `dmesg` |
| WebSocket won't connect | Verify bridge is running; check firewall isn't blocking port 3001 |
