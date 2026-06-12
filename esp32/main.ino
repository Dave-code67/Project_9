#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <esp_now.h>

WebServer server(80);
int globalMode = 1;

// Структура данных для связи с пультом по ESP-NOW
typedef struct struct_message {
    int type; 
    int val1; 
    int val2; 
} struct_message;

struct_message incomingData;

// Хранилище HTML-страницы сайта
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>InMoov Hand Control v3.0</title>
    <style>
        body {
            background-color: #0a0f1d;
            color: #00f0ff;
            font-family: 'Courier New', Courier, monospace;
            margin: 0;
            padding: 20px;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        h1 {
            text-shadow: 0 0 10px #00f0ff;
            margin-bottom: 5px;
        }
        .status-panel {
            border: 1px solid #00f0ff;
            padding: 10px 20px;
            margin-bottom: 20px;
            box-shadow: 0 0 15px rgba(0, 240, 255, 0.2);
            background: rgba(10, 15, 29, 0.8);
            width: 90%;
            max-width: 500px;
            text-align: center;
        }
        .control-block {
            border: 1px solid #005f73;
            padding: 15px;
            margin: 10px 0;
            width: 90%;
            max-width: 500px;
            background: #0d1527;
        }
        .control-block h3 { margin-top: 0; color: #94f3ff; }
        .slider-container {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin: 10px 0;
        }
        input[type=range] {
            flex-grow: 1;
            margin: 0 15px;
            accent-color: #00f0ff;
        }
        .btn {
            background: transparent;
            border: 1px solid #00f0ff;
            color: #00f0ff;
            padding: 10px 20px;
            cursor: pointer;
            font-family: inherit;
            transition: all 0.3s;
        }
        .btn:hover {
            background: #00f0ff;
            color: #0a0f1d;
            box-shadow: 0 0 10px #00f0ff;
        }
        .admin-link {
            margin-top: 30px;
            color: #005f73;
            cursor: pointer;
            text-decoration: underline;
            font-size: 0.9em;
        }
    </style>
</head>
<body>

    <h1>INMOOV_CORE_v3.0</h1>
    
    <div class="status-panel">
        СТАТУС СИСТЕМЫ: <span id="mode-status">ЗАГРУЗКА...</span>
    </div>

    <!-- Режим Конструктора -->
    <div class="control-block" id="constructor-ui">
        <h3>РЕЖИМ 01: КОНСТРУКТОР ЖЕСТОВ</h3>
        <p>Прямое пошаговое управление сервоприводами кисти:</p>
        
        <div class="slider-container">
            <span>БОЛЬШОЙ:</span>
            <input type="range" min="0" max="180" value="0" oninput="moveFinger(0, this.value)">
            <span id="val0">0°</span>
        </div>
        <div class="slider-container">
            <span>УКАЗАТ:</span>
            <input type="range" min="0" max="180" value="0" oninput="moveFinger(1, this.value)">
            <span id="val1">0°</span>
        </div>
        <div class="slider-container">
            <span>СРЕДНИЙ:</span>
            <input type="range" min="0" max="180" value="0" oninput="moveFinger(2, this.value)">
            <span id="val2">0°</span>
        </div>
        <div class="slider-container">
            <span>БЕЗЫМЯН:</span>
            <input type="range" min="0" max="180" value="0" oninput="moveFinger(3, this.value)">
            <span id="val3">0°</span>
        </div>
        <div class="slider-container">
            <span>МИЗИНЕЦ:</span>
            <input type="range" min="0" max="180" value="0" oninput="moveFinger(4, this.value)">
            <span id="val4">0°</span>
        </div>
    </div>

    <div class="admin-link" onclick="changeModeAdmin()">[ Сменить режим работы ] 🔒</div>

    <script>
        // Функция отправки угла пальца на сервер
        function moveFinger(finger, angle) {
            document.getElementById('val' + finger).innerText = angle + '°';
            fetch('/execute?finger=' + finger + '&angle=' + angle)
                .then(response => response.text())
                .then(data => {
                    if(data === "ANTI_JAM_TRIGGERED") {
                        alert("Система безопасности Anti-Jam заблокировала анатомически опасный жест!");
                    }
                });
        }

        // Запрос пароля для смены режима (из паспорта твоего проекта)
        function changeModeAdmin() {
            let pin = prompt("Введите сервисный код администратора:");
            if (pin === "47781842") {
                let newMode = prompt("Выберите режим:\n1 - Конструктор\n2 - Показ\n3 - Автохват лазером\n4 - БИО-ЭМГ");
                if(newMode >= 1 && newMode <= 4) {
                    fetch('/set_mode?value=' + newMode).then(() => updateStatus());
                }
            } else {
                alert("Ошибка доступа: неверный PIN-код!");
            }
        }

        // Обновление текущего режима на экране
        function updateStatus() {
            fetch('/get_mode')
                .then(res => res.text())
                .then(mode => {
                    let modes = {
                        "1": "01_РУЧНОЙ_КОНСТРУКТОР",
                        "2": "02_ДЕМО_ПОКАЗ_ЖЕСТОВ",
                        "3": "03_АВТОХВАТ_ЛАЗЕРОМ",
                        "4": "04_БИО_ЭМГ_ИНТЕРФЕЙС"
                    };
                    document.getElementById('mode-status').innerText = modes[mode] || "НЕИЗВЕСТНО";
                    
                    // Прячем ползунки, если режим не Конструктор
                    if(mode === "1") {
                        document.getElementById('constructor-ui').style.display = "block";
                    } else {
                        document.getElementById('constructor-ui').style.display = "none";
                    }
                });
        }

        // Опрашиваем сервер каждую секунду для синхронизации с пультом
        setInterval(updateStatus, 1000);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

// Функция отправки на Arduino по I2C
void sendToArduino(int type, int val1, int val2 = 0) {
  Wire.beginTransmission(0x08);
  Wire.write(type);
  Wire.write(val1);
  if (type == 1) Wire.write(val2);
  Wire.endTransmission();
}

// Матричный фильтр безопасности Anti-Jam System
bool checkAntiJam(int finger, int angle) {
  // Запрещаем отгибать ТОЛЬКО средний палец при остальных согнутых
  if (finger == 2 && angle > 90) { 
    return false; 
  }
  return true;
}

// Прием данных от Пульта по ESP-NOW
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
  
  if (incomingData.type == 0) { 
    globalMode = incomingData.val1;
    sendToArduino(0, globalMode);
  } 
  else if (incomingData.type == 1 && globalMode == 1) { 
    if (checkAntiJam(incomingData.val1, incomingData.val2)) {
      sendToArduino(1, incomingData.val1, incomingData.val2);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // Инициализация I2C (SDA=21, SCL=22)
  
  // Создание автономной Wi-Fi точки
  WiFi.softAP("InMoov_Hand_AP", "12345678password");
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Ошибка инициализации ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  // --- МАРШРУТЫ ВЕБ-СЕРВЕРА ---
  
  // Главная страница: отдаем наш упакованный HTML
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", INDEX_HTML);
  });

  // Запрос изменения режима от сайта
  server.on("/set_mode", HTTP_GET, []() {
    String modeStr = server.arg("value");
    globalMode = modeStr.toInt();
    sendToArduino(0, globalMode);
    server.send(200, "text/plain", "OK");
  });
  
  // Запрос текущего режима (для синхронизации)
  server.on("/get_mode", HTTP_GET, []() {
    server.send(200, "text/plain", String(globalMode));
  });

  // Запрос на движение пальца ползунком
  server.on("/execute", HTTP_GET, []() {
    int finger = server.arg("finger").toInt();
    int angle = server.arg("angle").toInt();
    
    if (globalMode == 1) {
      if (checkAntiJam(finger, angle)) {
        sendToArduino(1, finger, angle);
        server.send(200, "text/plain", "OK");
      } else {
        server.send(200, "text/plain", "ANTI_JAM_TRIGGERED");
      }
    } else {
      server.send(200, "text/plain", "WRONG_MODE");
    }
  });

  server.begin();
  Serial.println("[SYSTEM] Веб-сервер запущен. IP: 192.168.4.1");
}

void loop() {
  server.handleClient();
  
  // Логика работы автономного Режима 2 (Показ)
  if (globalMode == 2) {
    static unsigned long lastUpdate = 0;
    static int demoFinger = 0;
    static bool flexed = false;
    
    if (millis() - lastUpdate > 400) {
      lastUpdate = millis();
      sendToArduino(1, demoFinger, flexed ? 0 : 180);
      if (flexed) {
        demoFinger = (demoFinger + 1) % 5;
      }
      flexed = !flexed;
    }
  }
}
