const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const WebSocket = require('ws');
const http = require('http');
const express = require('express');

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

// Configure your serial port here
const SERIAL_PORT = '/dev/ttyUSB0'; // Change to COM3 on Windows, /dev/ttyUSB0 on Linux
const BAUD_RATE = 115200;

// Enable CORS for web access
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*');
  res.header('Access-Control-Allow-Headers', 'Origin, X-Requested-With, Content-Type, Accept');
  next();
});

let serialPort;
let parser;
let connectedClients = [];

// Current state to send to new connections
let currentState = {
  node: 1,
  state: 'IDLE',
  flow: 0,
  clientFlow: 0,
  learningMode: true,
  learningDays: 0,
  timestamp: Date.now()
};

// Initialize Serial Port
function initSerial() {
  try {
    serialPort = new SerialPort({
      path: SERIAL_PORT,
      baudRate: BAUD_RATE
    });

    parser = serialPort.pipe(new ReadlineParser({ delimiter: '\n' }));

    serialPort.on('open', () => {
      console.log(`✓ Serial port ${SERIAL_PORT} opened at ${BAUD_RATE} baud`);
    });

    parser.on('data', (line) => {
      // Check if it's JSON data from ESP32
      if (line.trim().startsWith('{')) {
        try {
          const data = JSON.parse(line);
          
          // Update current state
          currentState = {
            ...currentState,
            ...data,
            timestamp: Date.now()
          };
          
          // Broadcast to all connected WebSocket clients
          broadcastToClients({
            type: 'state_update',
            data: currentState
          });
          
          console.log(`[${data.learningMode ? 'LEARNING' : 'OPERATIONAL'}] State: ${data.state} | Flow: ${data.flow.toFixed(2)} L/min`);
        } catch (err) {
          // Not JSON, just regular serial output
          console.log(line);
        }
      } else {
        // Regular serial output (logs, debug info)
        console.log(line);
      }
    });

    serialPort.on('error', (err) => {
      console.error('Serial port error:', err.message);
    });

  } catch (err) {
    console.error('Failed to open serial port:', err.message);
    console.log('Available ports:');
    SerialPort.list().then(ports => {
      ports.forEach(port => {
        console.log(`  - ${port.path}`);
      });
    });
  }
}

// WebSocket connection handling
wss.on('connection', (ws) => {
  console.log('New WebSocket client connected');
  connectedClients.push(ws);
  
  // Send current state immediately to new client
  ws.send(JSON.stringify({
    type: 'initial_state',
    data: currentState
  }));
  
  ws.on('message', (message) => {
    try {
      const cmd = JSON.parse(message);
      
      // Handle commands from web interface
      switch(cmd.action) {
        case 'RESET':
          serialPort.write('RESET\n');
          console.log('Reset command sent to ESP32');
          break;
        case 'CUTOFF':
          serialPort.write('CUTOFF\n');
          console.log('Manual cutoff command sent to ESP32');
          break;
        case 'STATUS':
          serialPort.write('STATUS\n');
          break;
        case 'RESET_LEARNING':
          serialPort.write('RESET_LEARNING\n');
          console.log('Learning reset command sent to ESP32');
          break;
      }
    } catch (err) {
      console.error('Invalid WebSocket message:', err.message);
    }
  });
  
  ws.on('close', () => {
    console.log('WebSocket client disconnected');
    connectedClients = connectedClients.filter(client => client !== ws);
  });
});

function broadcastToClients(message) {
  const msgString = JSON.stringify(message);
  connectedClients.forEach(client => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(msgString);
    }
  });
}

// Simple REST API endpoints
app.get('/api/current', (req, res) => {
  res.json(currentState);
});

app.post('/api/command/:cmd', (req, res) => {
  const cmd = req.params.cmd.toUpperCase();
  
  if (['RESET', 'CUTOFF', 'STATUS', 'RESET_LEARNING'].includes(cmd)) {
    serialPort.write(cmd + '\n');
    res.json({ success: true, command: cmd });
  } else {
    res.status(400).json({ success: false, error: 'Invalid command' });
  }
});

// Start server
const PORT = 3001;
server.listen(PORT, () => {
  console.log('\n========================================');
  console.log('  AQUAGUARD SERIAL-TO-WEB BRIDGE');
  console.log('========================================');
  console.log(`HTTP Server: http://localhost:${PORT}`);
  console.log(`WebSocket Server: ws://localhost:${PORT}`);
  console.log(`Serial Port: ${SERIAL_PORT} @ ${BAUD_RATE} baud`);
  console.log('========================================\n');
  
  initSerial();
});

// List available serial ports on startup
SerialPort.list().then(ports => {
  console.log('Available Serial Ports:');
  ports.forEach(port => {
    console.log(`  ${port.path} - ${port.manufacturer || 'Unknown'}`);
  });
  console.log('');
});
