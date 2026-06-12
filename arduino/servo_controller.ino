#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Adafruit_VL53L0X.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

int currentMode = 1; // По умолчанию режим 1 (Конструктор)
unsigned long lastSensorTime = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(0x08); // Запускаем I2C с адресом 0x08 (Slave)
  Wire.onReceive(receiveEvent); // Функция обработки команд от ESP32
  
  pwm.begin();
  pwm.setPWMFreq(60); // Частота для сервоприводов MG996R
  
  if (!lox.begin()) {
    Serial.println("Ошибка инициализации лазерного дальномера!");
  }
  
  // Устанавливаем руку в начальное положение (0 градусов)
  for(int i=0; i<5; i++) setFingerAngle(i, 0);
}

void loop() {
  // Опрос датчиков в зависимости от текущего режима
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorTime > 100) {
    lastSensorTime = currentMillis;
    
    if (currentMode == 3) { // Режим 3: Автохват лазером
      VL53L0X_RangingMeasurementData_t measure;
      lox.getRangingMeasurement(&measure, false);
      if (measure.RangeStatus != 4 && measure.RangeMilliMeter < 100) { // Если объект ближе 10 см
        for(int i=0; i<5; i++) setFingerAngle(i, 180); // Сжимаем кулак
      } else {
        for(int i=0; i<5; i++) setFingerAngle(i, 0); // Расслабляем
      }
    } 
    else if (currentMode == 4) { // Режим 4: ЭМГ (Мышцы)
      int emgValue = analogRead(A0);
      if (emgValue > 600) { // Порог срабатывания мышцы
        for(int i=0; i<5; i++) setFingerAngle(i, 180);
      } else {
        for(int i=0; i<5; i++) setFingerAngle(i, 0);
      }
    }
  }
}

// Прием данных от главной ESP32
void receiveEvent(int howMany) {
  if (Wire.available() >= 2) {
    int type = Wire.read(); // Первый байт: тип посылки (0 - смена режима, 1 - управление пальцем)
    int value1 = Wire.read(); // Второй байт: номер режима или номер пальца
    
    if (type == 0) {
      currentMode = value1;
      Serial.print("Режим изменен на: "); Serial.println(currentMode);
    } 
    else if (type == 1 && currentMode == 1) { // Управляем пальцами только в режиме Конструктора
      int angle = Wire.read(); // Третий байт: угол
      setFingerAngle(value1, angle);
    }
  }
}

void setFingerAngle(int finger, int angle) {
  // Перевод угла 0-180 в импульс ШИМ (примерно 150-600)
  int pulse = map(angle, 0, 180, 150, 600);
  pwm.setPWM(finger, 0, pulse);
}
