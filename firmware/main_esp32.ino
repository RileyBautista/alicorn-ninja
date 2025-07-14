#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

const char* ssid = "GitHub Guest";
const char* password = "NO_PASSWORD_FOR_YOU!!!";

WiFiServer server(80);

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
sensors_event_t event;

float prevX = 0.0, prevY = 0.0, prevZ = 0.0;

unsigned long currentTime = millis();
unsigned long previousTime = 0; 
const long timeoutTime = 2000;

void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);

    if (!accel.begin()) {
    Serial.println("no adxl345");
        //while (1);
    }

    accel.setRange(ADXL345_RANGE_4_G);
    Serial.println("adxl345 ready");

    WiFi.begin(ssid, password);
    Serial.print("connecting");
    while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    }
    Serial.println("\nwifi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    server.begin();

    pinMode(32, OUTPUT);
    digitalWrite(32, HIGH);
}

String updatedPosition() {
    accel.getEvent(&event);
    unsigned long currentTime = millis();

    return "{\"x\":" + String(event.acceleration.x) + 
            ",\"y\":" + String(event.acceleration.y) + 
            ",\"z\":" + String(event.acceleration.z) + 
            ",\"timestamp\":" + String(currentTime) + "}";
}

void serveRootPage(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");

  client.println("Connection: close");
  client.println();
  client.println(R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>ESP32 Accelerometer</title>
<style>
    html, body { margin: 0; padding: 0; overflow: hidden; background: #eee; }
    canvas { display: block; }
    p { font-family: monospace; }
</style>
</head>
<body>
    <canvas id="canvas"></canvas>
    <p id="pos"></p>
    <p id="swipe"></p>
    <script>
    const canvas = document.getElementById('canvas');
    const ctx = canvas.getContext('2d');
    let baseX = null;
    let baseY = null;
    const threshold = 1.5;
    let lastSwipe = "No movement";
    let lastAngle = 0;
    let lastSwipeTime = 0;
    const swipeCooldown = 500;
    const sectorSize = 1;
    const bc = new BroadcastChannel("swipeChannel");

    function resizeCanvas() {
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
    }

    window.addEventListener('resize', resizeCanvas);
    resizeCanvas();

    function draw(x, y) {
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.fillStyle = 'blue';
        const cx = canvas.width / 2;
        const cy = canvas.height / 2;
        const drawX = cx + x * 10 - 10;
        const drawY = cy + y * 10 - 10;
        ctx.fillRect(drawX, drawY, 20, 20);
    }

    function detectSwipe(x, y) {
        const now = Date.now();
        
        if (baseX === null || baseY === null) {
            baseX = x;
            baseY = y;
            return "Base position set";
        }

        const diffX = x - baseX;
        const diffY = y - baseY;
        
        const distance = Math.sqrt(diffX * diffX + diffY * diffY);
        
        if (distance > threshold) {
            let angle = Math.atan2(-diffY, diffX) * 180 / Math.PI;
            if (angle < 0) angle += 360;
            
            const sectorAngle = Math.round(angle / sectorSize) * sectorSize;
            
            if (now - lastSwipeTime > swipeCooldown) {
                lastAngle = sectorAngle;
                lastSwipe = `${sectorAngle}° (dist: ${distance.toFixed(1)})`;
                lastSwipeTime = now;
                baseX = x;
                baseY = y;
            }
            bc.postMessage({angle: lastAngle, distance: distance, direction: lastSwipe});
            return lastSwipe;
        }
        
        baseX = baseX * 0.9 + x * 0.1;
        baseY = baseY * 0.9 + y * 0.1;
        
        return lastSwipe;
    }

    async function update() {
        try {
            const res = await fetch('/pos');
            const data = await res.json();
            draw(data.x, data.y);
            
            const swipeDirection = detectSwipe(data.x, data.y);
            
            document.getElementById("pos").innerHTML = "x: " + data.x + " y: " + data.y + " z: " + data.z;
            document.getElementById("swipe").innerHTML = "angle: " + swipeDirection;
        } catch (err) {
            console.error('failed to fetch position:', err);
        }
    }

    setInterval(update, 50);
</script>
</body>
</html>
)rawliteral");
}

void servePosEndpoint(WiFiClient& client) {
    String json = updatedPosition();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.print("Access-Control-Allow-Origin: *\r\n");
    client.print("Access-Control-Allow-Headers: Content-Type\r\n");
    client.print("Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n");
    client.println("Connection: close");  
    client.println();
    client.println(json);
}

void loop() {
    WiFiClient client = server.available();

    if (client) {
        currentTime = millis();
        previousTime = currentTime;
        Serial.println("new client");
        String currentLine = "";
        String request = "";

        while (client.connected() && currentTime - previousTime <= timeoutTime) {
            currentTime = millis();
            if (client.available()) {
                char c = client.read();
                request += c;
                if (c == '\n') {
                if (currentLine.length() == 0) {
                    if (request.indexOf("GET /pos") >= 0) {
                    servePosEndpoint(client);
                    } else {
                    serveRootPage(client);
                    }
                    break;
                } else {
                    currentLine = "";
                }
                } else if (c != '\r') {
                currentLine += c;
                }
            }
        }
        client.stop();
        Serial.println("client disconnect");
    }

  // accel.getEvent(&event);
  // float currX = event.acceleration.x;
  // float currY = event.acceleration.y;
  // float currZ = event.acceleration.z;

  // float deltaX = currX - prevX;
  // float deltaY = currY - prevY;
  // float deltaZ = currZ - prevZ;

  //Serial.print("ΔX: "); Serial.print(deltaX); Serial.print(" m/s^2 ");
  //Serial.print("ΔY: "); Serial.print(deltaY); Serial.print(" m/s^2 ");
  //Serial.print("ΔZ: "); Serial.println(deltaZ); Serial.println(" m/s^2");

  // prevX = currX;
  // prevY = currY;
  // prevZ = currZ;

  //delay(100);
}
