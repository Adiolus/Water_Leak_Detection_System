# Quick Website Integration Guide

## How to Connect Arduino Data to Your Existing Website

There are 3 methods to integrate the ESP32 data with your website. Choose based on your setup.

---

## Method 1: WebSocket (Real-Time, Recommended)

### Setup (One-Time)

1. **Start the Serial Bridge**:
   ```bash
   cd serial_bridge
   npm install
   npm start
   ```
   Bridge runs at: `ws://localhost:3001`

2. **Add to Your Website HTML**:

```html
<script>
// Connect to bridge
const ws = new WebSocket('ws://localhost:3001');

ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  
  if (message.type === 'state_update') {
    const data = message.data;
    
    // Update your UI elements
    document.getElementById('flow-rate').textContent = data.flow.toFixed(2);
    document.getElementById('state').textContent = data.state;
    document.getElementById('learning-status').textContent = 
      data.learningMode ? `Learning Day ${data.learningDays}/30` : 'Operational';
    
    // Change colors based on state
    if (data.state === 'CRITICAL_LEAK' || data.state === 'CUTOFF') {
      document.getElementById('status-indicator').style.backgroundColor = 'red';
    } else if (data.state === 'MINI_LEAK' || data.state === 'OVERUSAGE') {
      document.getElementById('status-indicator').style.backgroundColor = 'yellow';
    } else {
      document.getElementById('status-indicator').style.backgroundColor = 'green';
    }
  }
};

// Send commands to ESP32
function resetSystem() {
  ws.send(JSON.stringify({ action: 'RESET' }));
}

function emergencyCutoff() {
  ws.send(JSON.stringify({ action: 'CUTOFF' }));
}
</script>
```

### In Your HTML

```html
<div id="aquaguard-widget">
  <h3>Water Monitor</h3>
  <div id="status-indicator" style="width: 20px; height: 20px; border-radius: 50%;"></div>
  <p>Flow Rate: <span id="flow-rate">0.00</span> L/min</p>
  <p>State: <span id="state">IDLE</span></p>
  <p>Learning: <span id="learning-status">Day 0/30</span></p>
  
  <button onclick="resetSystem()">Reset</button>
  <button onclick="emergencyCutoff()">Emergency Cutoff</button>
</div>
```

---

## Method 2: REST API (Polling)

If you can't use WebSockets, poll the API every few seconds:

```javascript
// Fetch data every 2 seconds
setInterval(async () => {
  const response = await fetch('http://localhost:3001/api/current');
  const data = await response.json();
  
  // Update UI
  document.getElementById('flow-rate').textContent = data.flow.toFixed(2);
  document.getElementById('state').textContent = data.state;
}, 2000);

// Send commands
async function resetSystem() {
  await fetch('http://localhost:3001/api/command/RESET', { method: 'POST' });
}
```

---

## Method 3: Direct Serial (No Bridge)

If your website is hosted on the same computer as the Arduino:

### Python Backend Example

```python
import serial
import json
from flask import Flask, jsonify
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

ser = serial.Serial('/dev/ttyUSB0', 115200)  # Your port
current_state = {}

def read_serial():
    while True:
        line = ser.readline().decode('utf-8').strip()
        if line.startswith('{'):
            try:
                data = json.loads(line)
                current_state.update(data)
            except:
                pass

@app.route('/api/current')
def get_current():
    return jsonify(current_state)

if __name__ == '__main__':
    import threading
    threading.Thread(target=read_serial, daemon=True).start()
    app.run(port=3001)
```

Then use fetch() from your website as shown in Method 2.

---

## For Your Hackathon Demo

### Recommended Setup

1. **Use Method 1 (WebSocket)** - It's the most impressive because:
   - Real-time updates (no delay)
   - Bidirectional (website can control ESP32)
   - Shows understanding of modern web tech

2. **Run on Same Laptop**:
   ```
   Laptop → Serial → ESP32
   Laptop → Serial Bridge → WebSocket → Your Website
   ```

3. **If Presenting from Different Computer**:
   - Change `BRIDGE_URL` in your website code to:
     ```javascript
     const ws = new WebSocket('ws://YOUR_LAPTOP_IP:3001');
     ```
   - Get your laptop IP: `ipconfig` (Windows) or `ifconfig` (Linux/Mac)

---

## Quick Test Checklist

✅ **ESP32**:
- Upload code successfully
- See JSON output in Serial Monitor
- LEDs responding to states

✅ **Serial Bridge**:
- `npm start` runs without errors
- Prints "Serial port opened"
- Shows state changes in console

✅ **Website**:
- Opens without errors
- Connection dot turns green
- Flow values update in real-time
- Buttons send commands

---

## Troubleshooting

**"WebSocket connection failed"**
- Check bridge is running: `npm start`
- Check correct URL (localhost if same computer)
- Check firewall isn't blocking port 3001

**"Serial port not found"**
- Unplug/replug USB
- Check port name: Run `npm start`, it lists available ports
- Update `SERIAL_PORT` in `bridge.js`

**"No data updating"**
- Check Serial Monitor first - is ESP32 sending JSON?
- Check bridge console - is it receiving data?
- Check browser console (F12) - any errors?

---

## Sample Code for Common Frameworks

### React

```javascript
import { useEffect, useState } from 'react';

function AquaGuardWidget() {
  const [data, setData] = useState({ flow: 0, state: 'IDLE' });
  
  useEffect(() => {
    const ws = new WebSocket('ws://localhost:3001');
    
    ws.onmessage = (event) => {
      const message = JSON.parse(event.data);
      if (message.type === 'state_update') {
        setData(message.data);
      }
    };
    
    return () => ws.close();
  }, []);
  
  return (
    <div>
      <h3>Flow: {data.flow.toFixed(2)} L/min</h3>
      <p>State: {data.state}</p>
    </div>
  );
}
```

### Vue.js

```javascript
<template>
  <div>
    <h3>Flow: {{ data.flow.toFixed(2) }} L/min</h3>
    <p>State: {{ data.state }}</p>
  </div>
</template>

<script>
export default {
  data() {
    return {
      data: { flow: 0, state: 'IDLE' },
      ws: null
    }
  },
  mounted() {
    this.ws = new WebSocket('ws://localhost:3001');
    
    this.ws.onmessage = (event) => {
      const message = JSON.parse(event.data);
      if (message.type === 'state_update') {
        this.data = message.data;
      }
    };
  },
  beforeUnmount() {
    if (this.ws) this.ws.close();
  }
}
</script>
```

### Plain HTML + jQuery

```javascript
$(document).ready(function() {
  const ws = new WebSocket('ws://localhost:3001');
  
  ws.onmessage = function(event) {
    const message = JSON.parse(event.data);
    if (message.type === 'state_update') {
      $('#flow-rate').text(message.data.flow.toFixed(2));
      $('#state').text(message.data.state);
    }
  };
  
  $('#reset-btn').click(function() {
    ws.send(JSON.stringify({ action: 'RESET' }));
  });
});
```

---

## Data Format Reference

### Data Received from Bridge

```javascript
{
  "type": "state_update",  // or "initial_state"
  "data": {
    "node": 1,
    "state": "NORMAL_FLOW",
    "flow": 2.45,           // Master flow in L/min
    "clientFlow": 2.41,     // Client flow in L/min
    "learningMode": true,   // Is system still learning?
    "learningDays": 5,      // Days completed (0-30)
    "timestamp": 1234567890
  }
}
```

### Commands to Send to Bridge

```javascript
{ "action": "RESET" }          // Reset to IDLE
{ "action": "CUTOFF" }         // Emergency cutoff
{ "action": "STATUS" }         // Print learning status to serial
{ "action": "RESET_LEARNING" } // Clear all learned data
```

---

## Pro Tips for Demo Day

1. **Have Backup**: Save the example HTML page offline - if internet fails, you can still demo
2. **Mobile Responsive**: Test on phone - judges might want to see it on their devices
3. **Auto-Reconnect**: The example code already handles reconnection if connection drops
4. **Visual Feedback**: Make the LEDs visible in your physical setup - sync with website colors

Good luck! 🚀
