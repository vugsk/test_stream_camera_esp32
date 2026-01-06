# Интеграция с веб-сервером

Данный документ описывает, как настроить серверную часть для работы с ESP32-CAM.

---

## 🎯 Требования к серверу

- **Язык**: Любой (Node.js, Python, Go, Java, C#, PHP и т.д.)
- **HTTP Server**: Способный принимать POST запросы с бинарными данными
- **Порты**: Один открытый порт (например, 8081)
- **Пропускная способность**: Минимум 5 Mbps на устройство

---

## 📋 Обязательные endpoints

Сервер должен реализовать три HTTP endpoint:

1. `POST /stream` — прием JPEG кадров
2. `GET /api/camera` — отправка настроек камере
3. `POST /api/status` — прием телеметрии

---

## 🟢 Node.js/Express пример

### Установка зависимостей

```bash
npm init -y
npm install express body-parser multer
```

### Минимальный сервер

```javascript
const express = require('express');
const bodyParser = require('body-parser');
const fs = require('fs');

const app = express();
const PORT = 8081;

// Middleware для JSON
app.use(bodyParser.json());

// Middleware для бинарных данных (JPEG)
app.use('/stream', bodyParser.raw({ 
  type: 'image/jpeg',
  limit: '10mb'
}));

// === 1. Прием видеопотока ===
app.post('/stream', (req, res) => {
  const frameNumber = req.headers['x-frame'] || '0';
  const jpegData = req.body;
  
  console.log(`Frame ${frameNumber} received, size: ${jpegData.length} bytes`);
  
  // Сохранение последнего кадра
  fs.writeFileSync('latest_frame.jpg', jpegData);
  
  res.status(200).send();
});

// === 2. Отправка настроек камеры ===
let cameraSettings = {
  frameSize: 11,  // HD
  quality: 15,
  brightness: 0,
  contrast: 0,
  saturation: 0,
  fps: 30,
  vflip: false,
  hmirror: false,
  streaming: true
};

app.get('/api/camera', (req, res) => {
  res.json(cameraSettings);
});

// Изменение настроек через POST
app.post('/api/camera/settings', (req, res) => {
  cameraSettings = { ...cameraSettings, ...req.body };
  console.log('Settings updated:', cameraSettings);
  res.json({ success: true });
});

// === 3. Прием статуса устройства ===
const devices = new Map();

app.post('/api/status', (req, res) => {
  const status = req.body;
  const deviceId = status.device_id;
  
  devices.set(deviceId, {
    ...status,
    lastSeen: new Date()
  });
  
  console.log(`Device ${deviceId}: ${status.frames_sent} frames, RSSI: ${status.wifi_rssi} dBm`);
  
  res.status(200).send();
});

// Просмотр статусов всех устройств
app.get('/api/devices', (req, res) => {
  const deviceList = Array.from(devices.values());
  res.json(deviceList);
});

// Запуск сервера
app.listen(PORT, '0.0.0.0', () => {
  console.log(`ESP32-CAM Server listening on port ${PORT}`);
  console.log(`Stream endpoint: http://localhost:${PORT}/stream`);
});
```

### Запуск

```bash
node server.js
```

---

## 🐍 Python/Flask пример

### Установка зависимостей

```bash
pip install flask
```

### Минимальный сервер

```python
from flask import Flask, request, jsonify
from datetime import datetime

app = Flask(__name__)

# === 1. Прием видеопотока ===
@app.route('/stream', methods=['POST'])
def receive_stream():
    frame_number = request.headers.get('X-Frame', '0')
    jpeg_data = request.data
    
    print(f"Frame {frame_number} received, size: {len(jpeg_data)} bytes")
    
    # Сохранение последнего кадра
    with open('latest_frame.jpg', 'wb') as f:
        f.write(jpeg_data)
    
    return '', 200

# === 2. Отправка настроек камеры ===
camera_settings = {
    'frameSize': 11,  # HD
    'quality': 15,
    'brightness': 0,
    'contrast': 0,
    'saturation': 0,
    'fps': 30,
    'vflip': False,
    'hmirror': False,
    'streaming': True
}

@app.route('/api/camera', methods=['GET'])
def get_camera_settings():
    return jsonify(camera_settings)

@app.route('/api/camera/settings', methods=['POST'])
def update_camera_settings():
    global camera_settings
    camera_settings.update(request.json)
    print(f"Settings updated: {camera_settings}")
    return jsonify({'success': True})

# === 3. Прием статуса устройства ===
devices = {}

@app.route('/api/status', methods=['POST'])
def receive_status():
    status = request.json
    device_id = status.get('device_id')
    
    devices[device_id] = {
        **status,
        'lastSeen': datetime.now().isoformat()
    }
    
    print(f"Device {device_id}: {status['frames_sent']} frames, RSSI: {status['wifi_rssi']} dBm")
    
    return '', 200

@app.route('/api/devices', methods=['GET'])
def get_devices():
    return jsonify(list(devices.values()))

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8081, debug=False)
```

### Запуск

```bash
python server.py
```

---

## 🎨 Расширенные возможности

### 1. Веб-интерфейс для просмотра

```javascript
// public/index.html
<!DOCTYPE html>
<html>
<head>
    <title>ESP32-CAM Viewer</title>
</head>
<body>
    <h1>Live Stream</h1>
    <img id="stream" src="/latest_frame" style="width: 100%; max-width: 1280px;">
    
    <script>
        // Обновление изображения каждые 100ms
        setInterval(() => {
            document.getElementById('stream').src = '/latest_frame?' + Date.now();
        }, 100);
    </script>
</body>
</html>

// В Express добавить:
app.get('/latest_frame', (req, res) => {
    res.sendFile(__dirname + '/latest_frame.jpg');
});

app.use(express.static('public'));
```

### 2. Запись видео (ffmpeg)

```javascript
const { spawn } = require('child_process');

// Создание ffmpeg процесса для записи
const ffmpeg = spawn('ffmpeg', [
    '-f', 'image2pipe',
    '-vcodec', 'mjpeg',
    '-r', '30',
    '-i', '-',
    '-vcodec', 'libx264',
    '-preset', 'ultrafast',
    '-f', 'mp4',
    'output.mp4'
]);

app.post('/stream', (req, res) => {
    const jpegData = req.body;
    
    // Запись в ffmpeg
    ffmpeg.stdin.write(jpegData);
    
    res.status(200).send();
});
```

### 3. WebSocket стриминг клиентам

```javascript
const WebSocket = require('ws');
const wss = new WebSocket.Server({ port: 8082 });

const clients = new Set();

wss.on('connection', (ws) => {
    clients.add(ws);
    console.log('Client connected');
    
    ws.on('close', () => {
        clients.delete(ws);
        console.log('Client disconnected');
    });
});

app.post('/stream', (req, res) => {
    const jpegData = req.body;
    
    // Отправка всем подключенным клиентам
    clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(jpegData);
        }
    });
    
    res.status(200).send();
});
```

### 4. Детекция движения (OpenCV)

```python
import cv2
import numpy as np

previous_frame = None
motion_threshold = 5000  # Пикселей изменений

@app.route('/stream', methods=['POST'])
def receive_stream():
    global previous_frame
    
    jpeg_data = request.data
    
    # Декодирование JPEG
    nparr = np.frombuffer(jpeg_data, np.uint8)
    frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    
    # Детекция движения
    if previous_frame is not None:
        diff = cv2.absdiff(previous_frame, gray)
        _, thresh = cv2.threshold(diff, 25, 255, cv2.THRESH_BINARY)
        motion_pixels = np.sum(thresh) / 255
        
        if motion_pixels > motion_threshold:
            print(f"MOTION DETECTED! Changed pixels: {motion_pixels}")
            # Сохранение кадра с движением
            cv2.imwrite(f'motion_{datetime.now().timestamp()}.jpg', frame)
    
    previous_frame = gray.copy()
    
    return '', 200
```

---

## 🗄️ Хранение данных

### База данных для телеметрии (MongoDB)

```javascript
const MongoClient = require('mongodb').MongoClient;
const url = 'mongodb://localhost:27017';
const dbName = 'esp32cam';

let db;
MongoClient.connect(url, (err, client) => {
    db = client.db(dbName);
    console.log('Connected to MongoDB');
});

app.post('/api/status', async (req, res) => {
    const status = req.body;
    
    await db.collection('device_status').insertOne({
        ...status,
        timestamp: new Date()
    });
    
    res.status(200).send();
});
```

### S3 для хранения кадров (AWS)

```javascript
const AWS = require('aws-sdk');
const s3 = new AWS.S3();

app.post('/stream', async (req, res) => {
    const frameNumber = req.headers['x-frame'];
    const jpegData = req.body;
    
    // Загрузка в S3
    await s3.putObject({
        Bucket: 'my-esp32cam-frames',
        Key: `frames/${Date.now()}-${frameNumber}.jpg`,
        Body: jpegData,
        ContentType: 'image/jpeg'
    }).promise();
    
    res.status(200).send();
});
```

---

## 🔒 Безопасность

### Basic Authentication

```javascript
const basicAuth = require('express-basic-auth');

app.use(basicAuth({
    users: { 'admin': 'supersecret' },
    challenge: true
}));
```

### Token Authentication

```javascript
const TOKEN = 'your-secret-token';

app.use((req, res, next) => {
    const token = req.headers['authorization'];
    if (token !== `Bearer ${TOKEN}`) {
        return res.status(401).send('Unauthorized');
    }
    next();
});
```

### HTTPS (рекомендуется для продакшн)

```javascript
const https = require('https');
const fs = require('fs');

const options = {
    key: fs.readFileSync('server.key'),
    cert: fs.readFileSync('server.cert')
};

https.createServer(options, app).listen(8081);
```

---

## 🚀 Развертывание

### Docker

```dockerfile
FROM node:16-alpine

WORKDIR /app
COPY package*.json ./
RUN npm install
COPY . .

EXPOSE 8081
CMD ["node", "server.js"]
```

```bash
docker build -t esp32cam-server .
docker run -p 8081:8081 esp32cam-server
```

### systemd (Linux)

```ini
[Unit]
Description=ESP32-CAM Server
After=network.target

[Service]
Type=simple
User=www-data
WorkingDirectory=/opt/esp32cam-server
ExecStart=/usr/bin/node server.js
Restart=always

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable esp32cam-server
sudo systemctl start esp32cam-server
```

---

## 📊 Мониторинг

### Prometheus метрики

```javascript
const promClient = require('prom-client');
const register = new promClient.Registry();

const framesReceived = new promClient.Counter({
    name: 'esp32cam_frames_received_total',
    help: 'Total frames received',
    registers: [register]
});

app.post('/stream', (req, res) => {
    framesReceived.inc();
    // ... остальной код
});

app.get('/metrics', (req, res) => {
    res.set('Content-Type', register.contentType);
    res.end(register.metrics());
});
```

---

## 🧪 Тестирование сервера

```bash
# Тест приема кадра
curl -X POST http://localhost:8081/stream \
  -H "Content-Type: image/jpeg" \
  -H "X-Frame: 1" \
  --data-binary @test.jpg

# Тест получения настроек
curl http://localhost:8081/api/camera

# Тест отправки статуса
curl -X POST http://localhost:8081/api/status \
  -H "Content-Type: application/json" \
  -d '{"device_id":"AA:BB:CC:DD:EE:FF","frames_sent":100}'
```

