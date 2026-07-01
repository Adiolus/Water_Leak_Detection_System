# Hardware Wiring Guide

## Component List

### Master ESP32 Node
- 1x ESP32 DevKit (30-pin or 38-pin)
- 1x YF-S201 Flow Sensor
- 1x DS3231 RTC Module (I2C)
- 1x SSD1306 OLED Display 128x64 (I2C)
- 1x 12V Solenoid Valve
- 1x 5V Relay Module
- 3x LEDs (Red, Yellow, Green)
- 3x 220Ω Resistors
- 1x Breadboard
- Jumper wires
- 12V Power Supply for solenoid

### Client ESP32 Node
- 1x ESP32 DevKit
- 1x YF-S201 Flow Sensor
- Jumper wires

---

## Master ESP32 Wiring

### Flow Sensor (YF-S201)
```
Flow Sensor → ESP32
-----------------------
Red (VCC)   → 5V or 3.3V
Black (GND) → GND
Yellow      → GPIO 4
```

### RTC Module (DS3231)
```
RTC Module → ESP32
-----------------------
VCC → 3.3V
GND → GND
SDA → GPIO 21 (default I2C SDA)
SCL → GPIO 22 (default I2C SCL)
```

### OLED Display (SSD1306)
```
OLED → ESP32
-----------------------
VCC → 3.3V
GND → GND
SDA → GPIO 21 (shares with RTC)
SCL → GPIO 22 (shares with RTC)
```

### Status LEDs
```
LED → ESP32
-----------------------
Green LED:
  Anode (+) → 220Ω resistor → GPIO 12
  Cathode (-) → GND

Yellow LED:
  Anode (+) → 220Ω resistor → GPIO 13
  Cathode (-) → GND

Red LED:
  Anode (+) → 220Ω resistor → GPIO 14
  Cathode (-) → GND
```

### Relay Module (for Solenoid Valve)
```
Relay Module → ESP32
-----------------------
VCC → 5V
GND → GND
IN  → GPIO 5

Relay → Solenoid Valve
-----------------------
COM → 12V+ from power supply
NO  → Solenoid valve positive terminal
Solenoid valve negative → 12V- from power supply
```

**IMPORTANT:** 
- Use a relay rated for at least 12V/2A
- The solenoid valve needs external 12V power (DO NOT connect to ESP32 power)
- "NO" means Normally Open - valve is open when relay is OFF

---

## Client ESP32 Wiring

### Flow Sensor (YF-S201)
```
Flow Sensor → ESP32
-----------------------
Red (VCC)   → 5V or 3.3V
Black (GND) → GND
Yellow      → GPIO 4
```

That's it for the client! It only needs one sensor.

---

## Complete Pinout Summary

### Master ESP32
| Component | Pin | Notes |
|-----------|-----|-------|
| Flow Sensor | GPIO 4 | Interrupt pin |
| Relay (Solenoid) | GPIO 5 | Digital output |
| Green LED | GPIO 12 | Via 220Ω resistor |
| Yellow LED | GPIO 13 | Via 220Ω resistor |
| Red LED | GPIO 14 | Via 220Ω resistor |
| I2C SDA (RTC + OLED) | GPIO 21 | Shared bus |
| I2C SCL (RTC + OLED) | GPIO 22 | Shared bus |

### Client ESP32
| Component | Pin | Notes |
|-----------|-----|-------|
| Flow Sensor | GPIO 4 | Interrupt pin |

---

## Power Requirements

### Master Node
- ESP32: 500mA @ 5V (via USB or VIN pin)
- OLED: 20mA @ 3.3V
- RTC: 5mA @ 3.3V (has coin battery backup)
- LEDs: 3 × 20mA = 60mA @ 3.3V
- Relay Module: 70mA @ 5V
- **Total: ~650mA @ 5V**

**Power Options:**
1. USB cable from laptop (easiest for demo)
2. 5V 1A wall adapter to VIN pin
3. 5V power bank (portable demo)

### Client Node
- ESP32: 500mA @ 5V
- **Total: ~500mA @ 5V**

**Power Option:** USB cable or 5V adapter

### Solenoid Valve
- Typical: 12V @ 0.5-2A
- **MUST have separate 12V power supply**
- Do NOT attempt to power from ESP32

---

## Assembly Steps

### Step 1: Test Components Individually

1. **Test ESP32**:
   - Upload blink sketch
   - Verify it works

2. **Test Flow Sensor**:
   - Connect to ESP32
   - Upload simple counter sketch
   - Blow into sensor, verify pulses counted

3. **Test RTC**:
   - Upload RTC test sketch
   - Set time
   - Verify time keeps after power cycle

4. **Test OLED**:
   - Upload OLED test sketch
   - Verify display shows text

### Step 2: Assemble Master Node

1. **Breadboard Layout**:
   ```
   [ESP32 on left side of breadboard]
   [OLED and RTC on right side]
   [LEDs in a row below]
   [Relay module off breadboard, wired separately]
   ```

2. **I2C Bus** (RTC + OLED):
   - Both share GPIO 21 (SDA) and GPIO 22 (SCL)
   - Connect all VCC to 3.3V rail
   - Connect all GND to ground rail

3. **LEDs**:
   - Longer leg (anode) → resistor → GPIO
   - Shorter leg (cathode) → GND

4. **Power**:
   - Connect breadboard power rails to ESP32 GND and 3.3V pins

### Step 3: Assemble Client Node

Simple! Just connect one flow sensor to GPIO 4.

### Step 4: Connect Solenoid Valve

**CRITICAL SAFETY:**
- Double-check relay is rated for 12V
- Keep 12V circuit completely separate from ESP32
- Test relay clicking sound before connecting valve

```
12V Supply (+) → Relay COM
Relay NO → Solenoid (+)
Solenoid (-) → 12V Supply (-)
```

When ESP32 sends HIGH to GPIO 5 → Relay closes → Valve closes

---

## Physical Demo Setup

### Arrangement for Judges

```
┌─────────────────────┐
│  Laptop (Bridge)    │
│  Running npm start  │
└──────────┬──────────┘
           │ USB Cable
           ▼
┌─────────────────────┐       ┌──────────────┐
│   Master ESP32      │◄──────┤ Client ESP32 │
│   + OLED + LEDs     │ WiFi  └──────────────┘
│   + RTC             │  (ESP-NOW)
└─────────┬───────────┘
          │
          ▼
    [Solenoid Valve]
          │
    [Water Source]
```

**Demo Flow Path:**
1. Water enters → Flow Sensor 1 (Master)
2. Water continues → Solenoid Valve (can close)
3. Water exits → Flow Sensor 2 (Client)

**Visual Elements for Judges:**
- OLED shows state in real-time
- 3 LEDs visible from distance (green/yellow/red)
- Website on projector/screen
- Solenoid valve visible (they can hear it click)

---

## Troubleshooting

### "Flow sensor not detecting"
- Check it's getting 3.3V or 5V power
- Verify yellow wire is on GPIO 4
- Blow into it - should see pulses
- Check sensor has water wheel inside (some cheap ones don't)

### "I2C devices not found"
- Run I2C scanner sketch to find addresses
- OLED usually at 0x3C
- RTC usually at 0x68
- Check SDA/SCL not swapped

### "Relay not clicking"
- Measure voltage on IN pin when triggered (should be 3.3V)
- Check relay VCC has 5V
- Try different relay module (some need signal inverting)

### "ESP32 keeps resetting"
- Not enough power - use better USB cable or power supply
- Solenoid drawing too much current - use separate 12V supply
- Bad ground connection - ensure all grounds are common

### "LEDs very dim"
- Wrong resistor value - use 220Ω
- LED backwards - flip it
- GPIO not going HIGH - check code

---

## Alternative Components

If you can't find exact components:

### Flow Sensors
- YF-S201 (recommended, cheap)
- YF-S402 (larger pipes)
- Hall-effect sensors (3/8" or 1/2" pipe)

### RTC Modules
- DS3231 (recommended, accurate)
- DS1307 (cheaper, less accurate)
- PCF8523 (alternative)

### OLED Displays
- 128x64 I2C (recommended)
- 128x32 I2C (smaller)
- Can skip if demo is just on laptop screen

### Relay Modules
- 1-channel 5V relay (recommended)
- SSR (solid state relay) for silent operation
- Can simulate with LED if no valve available

---

## Bill of Materials (India Prices)

| Item | Approx Price | Where to Buy |
|------|-------------|--------------|
| ESP32 DevKit | ₹350-500 | Robu.in, Amazon |
| YF-S201 Flow Sensor | ₹150-200 | Robu.in |
| DS3231 RTC | ₹100-150 | Robu.in |
| SSD1306 OLED | ₹200-300 | Robu.in |
| 5V Relay Module | ₹50-80 | Local electronics |
| 12V Solenoid Valve | ₹200-400 | Hardware store |
| LEDs (pack of 100) | ₹50 | Local electronics |
| Resistors (pack) | ₹30 | Local electronics |
| Breadboard | ₹80-120 | Local electronics |
| Jumper wires | ₹60-100 | Local electronics |
| 12V 2A adapter | ₹150-250 | Local electronics |
| **TOTAL** | **~₹1,500-2,500** | |

---

## Quick Assembly Checklist for Demo Day

☐ ESP32 powered and uploading code
☐ Flow sensor connected and reading pulses
☐ RTC set to correct time
☐ OLED displaying correctly
☐ All 3 LEDs lighting up in correct states
☐ Relay clicking when testing cutoff
☐ Solenoid valve closing when relay triggers
☐ Client ESP32 communicating (check serial)
☐ Serial bridge running on laptop
☐ Website connecting to bridge
☐ Water source ready (bucket + pump or tap)
☐ Drain container ready
☐ All cables secured (no loose connections)
☐ Extra USB cables and power banks as backup

---

## Safety Notes

⚠️ **IMPORTANT**:
- Never connect 12V directly to ESP32 pins
- Always use a relay for solenoid valve
- Keep water away from ESP32 and breadboard
- Have towels ready for demo
- Test in dry-run first without water
- Secure all connections with tape for transport

Good luck with your build! 🔧
