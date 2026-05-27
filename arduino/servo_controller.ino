#include <Wire.h>
#include <Adafruit_PWMServoDriver.h> 

#define ARDUINO_I2C_ADDR 8

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

#define THUMB 0
#define INDEX 1
#define MIDDLE 2
#define RING 3
#define PINKY 4

#define SERVO_MIN 150
#define SERVO_MAX 600

uint8_t fingerPosition[5] = {0, 0, 0, 0, 0};

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\n[ИНИЦИАЛИЗАЦИЯ ARDUINO UNO]");

  Wire.begin(ARDUINO_I2C_ADDR);
  Wire.onReceive(handleI2CData);
  Serial.println("[OK] I2C Slave инициализирован на адресе 0x08");

  if (!pwm.begin()) {
    Serial.println("[ERROR] PCA9685 не найден! Проверь подключение.");
    while(1) delay(100);
  }
  Serial.println("[OK] PCA9685 инициализирован");

  pwm.setPWMFreq(60);
  delay(100);

  setAllFingers(0);
  Serial.println("[OK] Все пальцы разогнуты (0 градусов)");
  
  Serial.println("[READY] Arduino готов принимать команды!\n");
}

void loop() {
  delay(10);
}

void handleI2CData(int byteCount) {
  if (byteCount < 2) {
    Serial.println("[WARNING] Неполная команда получена");
    return;
  }

  uint8_t fingerNum = Wire.read();
  uint8_t angle = Wire.read();
  Serial.print("[I2C] Палец: ");
  Serial.print(fingerNum);
  Serial.print(" | Угол: ");
  Serial.println(angle);

  if (fingerNum == 0) {
    setAllFingers(angle);
    Serial.println("  -> Установлена позиция для ВСЕ ПАЛЬЦЫ");
  } 
  else if (fingerNum >= 1 && fingerNum <= 5) {
    setFinger(fingerNum - 1, angle);
    Serial.print("  -> Палец #");
    Serial.print(fingerNum);
    Serial.println(" установлен");
  } 
  else {
    Serial.println("[ERROR] Неверный номер пальца!");
  }
}

void setFinger(uint8_t finger, uint8_t angle) {
  if (finger > 4 || angle > 180) {
    Serial.println("[ERROR] Неверные параметры!");
    return;
  }
  
  fingerPosition[finger] = angle;
  
  uint16_t pwmValue = map(angle, 0, 180, SERVO_MIN, SERVO_MAX);

  pwm.setPWM(finger, 0, pwmValue);
  
  Serial.print("  Позиция обновлена: ");
  Serial.print(angle);
  Serial.print("° -> PWM: ");
  Serial.println(pwmValue);
}

void setAllFingers(uint8_t angle) {
  if (angle > 180) {
    Serial.println("[ERROR] Угол выше 180°!");
    return;
  }
  
  uint16_t pwmValue = map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
  
  for (int i = 0; i < 5; i++) {
    fingerPosition[i] = angle;
    pwm.setPWM(i, 0, pwmValue);
  }
  
  Serial.print("  Все пальцы установлены на: ");
  Serial.print(angle);
  Serial.print("° -> PWM: ");
  Serial.println(pwmValue);
}

void moveFingerSmooth(uint8_t finger, uint8_t targetAngle, uint16_t speed = 30) {
  if (finger > 4 || targetAngle > 180) return;
  
  uint8_t currentAngle = fingerPosition[finger];
  
  if (currentAngle < targetAngle) {
    for (uint8_t angle = currentAngle; angle <= targetAngle; angle++) {
      setFinger(finger, angle);
      delay(speed);
    }
  } 
  else if (currentAngle > targetAngle) {
    for (uint8_t angle = currentAngle; angle >= targetAngle; angle--) {
      setFinger(finger, angle);
      delay(speed);
    }
  }
}

uint8_t getFingerPosition(uint8_t finger) {
  if (finger < 5) return fingerPosition[finger];
  return 255;
}

void testAllFingers() {
  Serial.println("\n[TEST] Начинаю тестирование пальцев...");
  
  const char* fingerNames[] = {"БОЛЬШОЙ", "УКАЗАТЕЛЬНЫЙ", "СРЕДНИЙ", "БЕЗЫМЯННЫЙ", "МИЗИНЕЦ"};
  
  for (int f = 0; f < 5; f++) {
    Serial.print("[TEST] Тестирую палец: ");
    Serial.println(fingerNames[f]);

    setFinger(f, 0);
    delay(500);

    setFinger(f, 180);
    delay(500);

    setFinger(f, 0);
    delay(300);
  }
  
  Serial.println("[TEST] Тестирование завершено!\n");
}

void testWave() {
  Serial.println("[TEST] Волна по пальцам...");
  
  for (int wave = 0; wave < 2; wave++) {
    for (int f = 0; f < 5; f++) {
      moveFingerSmooth(f, 90, 50);
      delay(200);
    }
    for (int f = 4; f >= 0; f--) {
      moveFingerSmooth(f, 0, 50);
      delay(200);
    }
  }
  
  Serial.println("[TEST] Волна завершена!\n");
}
