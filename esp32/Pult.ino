#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Пины для 6 кнопок
const int buttons[6] = {13, 12, 14, 27, 26, 25}; 

// Адрес главной ESP32 (замени на реальный MAC-адрес твоей первой платы)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
    int type; 
    int val1; 
    int val2; 
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

int selectedFinger = -1;
unsigned long actionTimer = 0;
bool waitingForFlex = false;
bool waitingForCombo = false;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  for(int i=0; i<6; i++) {
    pinMode(buttons[i], INPUT_PULLUP); // Включаем встроенные резисторы
  }

  if (esp_now_init() != ESP_OK) return;
  
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  display.clearDisplay();
  display.setCursor(0,0);
  
  // Проверка таймера на 2 секунды
  if ((waitingForFlex || waitingForCombo) && (millis() - actionTimer > 2000)) {
    if (waitingForFlex && selectedFinger != -1) {
      // Время вышло — разгибаем палец в 0 градусов
      myData.type = 1; myData.val1 = selectedFinger; myData.val2 = 0;
      esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
      display.println("Время вышло! Ресет.");
    } 
    else if (waitingForCombo) {
      // Время вышло — включаем режим показа 2
      myData.type = 0; myData.val1 = 2;
      esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
      display.println("Режим 2: Показ");
    }
    waitingForFlex = false; waitingForCombo = false; selectedFinger = -1;
    display.display();
    delay(1000);
    return;
  }

  // Опрос кнопок
  for (int i = 0; i < 6; i++) {
    if (digitalRead(buttons[i]) == LOW) { // Кнопка нажата
      delay(200); // Антидребезг контактов
      
      if (!waitingForFlex && !waitingForCombo) {
        if (i < 5) { // Нажата кнопка пальца (1-5) -> (в коде 0-4)
          selectedFinger = i;
          waitingForFlex = true;
          actionTimer = millis();
        } else if (i == 5) { // Нажата 6-я кнопка (Режим)
          waitingForCombo = true;
          actionTimer = millis();
        }
      } 
      else if (waitingForFlex) { // Мы уже выбрали палец, теперь выбираем сгиб кнопками 1-4
        if (i < 4) {
          int angle = map(i + 1, 1, 4, 45, 180); // 1->45, 4->180 градусов
          myData.type = 1; myData.val1 = selectedFinger; myData.val2 = angle;
          esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
          waitingForFlex = false; selectedFinger = -1;
        }
      } 
      else if (waitingForCombo) { // Ждем комбо для смены режима (6 + 1..4) или жеста (6 + 5)
        if (i < 4) { // 6 + 1..4 -> Смена режима
          myData.type = 0; myData.val1 = i + 1;
          esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
        } else if (i == 4) { // 6 + 5 -> Жест Рок
          // Логика отправки жеста Рок
        } else if (i == 5) { // 6 + 6 -> Сброс всех пальцев на 0
          for(int f=0; f<5; f++) {
            myData.type = 1; myData.val1 = f; myData.val2 = 0;
            esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
          }
        }
        waitingForCombo = false;
      }
    }
  }

  // Вывод подсказок на экран пульта
  if (waitingForFlex) {
    display.print("Палец: "); display.println(selectedFinger + 1);
    display.println("Жми 1-4 для сгиба...");
  } else if (waitingForCombo) {
    display.println("РЕЖИМЫ: 1-Констр, 4-ЭКГ");
    display.println("5-Рок, 6-Ресет");
  } else {
    display.println("Пульт InMoov Готов");
    display.println("Выберите палец 1-5");
  }
  display.display();
}
