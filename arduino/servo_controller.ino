#include <Wire.h>
#include <Adafruit_PWMServoDriver.h> 

// ========== КОНФИГУРАЦИЯ ==========

// I2C адрес Arduino (слушает команды от ESP32)
#define ARDUINO_I2C_ADDR 8

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// PCA9685 объект для управления серво
Adafruit_PCA9685 pwm = Adafruit_PCA9685();

// Адреса пальцев на PCA9685 (каналы 0-4)
#define THUMB 0      // Большой палец
#define INDEX 1      // Указательный
#define MIDDLE 2     // Средний
#define RING 3       // Безымянный
#define PINKY 4      // Мизинец

// Диапазон PWM для серво MG996R (0-180 градусов)
// Для PCA9685 частота обычно 60 Гц, диапазон PWM 150-600
#define SERVO_MIN 150   // 0 градусов (разогнут)
#define SERVO_MAX 600   // 180 градусов (согнут)

// Массив текущих позиций пальцев (0-180)
uint8_t fingerPosition[5] = {0, 0, 0, 0, 0};

// ========== ИНИЦИАЛИЗАЦИЯ ==========

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\n[ИНИЦИАЛИЗАЦИЯ ARDUINO UNO]");
  
  // Инициализируем I2C как slave на адресе 8
  Wire.begin(ARDUINO_I2C_ADDR);
  Wire.onReceive(handleI2CData);
  Serial.println("[OK] I2C Slave инициализирован на адресе 0x08");
  
  // Инициализируем PCA9685 (адрес по умолчанию 0x40)
  if (!pwm.begin()) {
    Serial.println("[ERROR] PCA9685 не найден! Проверь подключение.");
    while(1) delay(100); // Зависаем, если шилда нет
  }
  Serial.println("[OK] PCA9685 инициализирован");
  
  // Устанавливаем частоту PWM 60 Гц (стандартная для серво)
  pwm.setPWMFreq(60);
  delay(100);
  
  // Устанавливаем начальные позиции (все пальцы разогнуты)
  setAllFingers(0);
  Serial.println("[OK] Все пальцы разогнуты (0 градусов)");
  
  Serial.println("[READY] Arduino готов принимать команды!\n");
}

// ========== ГЛАВНЫЙ ЦИКЛ ==========

void loop() {
  delay(10);
  // I2C обработка происходит через прерывание (handleI2CData)
}

// ========== ОБРАБОТКА I2C КОМАНД ==========

void handleI2CData(int byteCount) {
  if (byteCount < 2) {
    Serial.println("[WARNING] Неполная команда получена");
    return;
  }
  
  // Читаем 2 байта: палец и позиция
  uint8_t fingerNum = Wire.read();  // 0-5 (0=кулак, 1-5=пальцы)
  uint8_t angle = Wire.read();      // 0-180 градусов
  
  Serial.print("[I2C] Палец: ");
  Serial.print(fingerNum);
  Serial.print(" | Угол: ");
  Serial.println(angle);
  
  // Обработка команды
  if (fingerNum == 0) {
    // Команда 0 = управление ВСЕЙ кистью
    setAllFingers(angle);
    Serial.println("  -> Установлена позиция для ВСЕ ПАЛЬЦЫ");
  } 
  else if (fingerNum >= 1 && fingerNum <= 5) {
    // Команда 1-5 = управление конкретным пальцем
    setFinger(fingerNum - 1, angle);
    Serial.print("  -> Палец #");
    Serial.print(fingerNum);
    Serial.println(" установлен");
  } 
  else {
    Serial.println("[ERROR] Неверный номер пальца!");
  }
}

// ========== ФУНКЦИИ УПРАВЛЕНИЯ СЕРВО ==========

/**
 * Устанавливает позицию одного пальца
 * finger: 0-4 (индекс в массиве)
 * angle: 0-180 градусов
 */
void setFinger(uint8_t finger, uint8_t angle) {
  // Проверяем диапазон
  if (finger > 4 || angle > 180) {
    Serial.println("[ERROR] Неверные параметры!");
    return;
  }
  
  // Сохраняем позицию
  fingerPosition[finger] = angle;
  
  // Преобразуем градусы в PWM значение для PCA9685
  // Формула: PWM = SERVO_MIN + (angle / 180) * (SERVO_MAX - SERVO_MIN)
  uint16_t pwmValue = map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
  
  // Отправляем на PCA9685
  pwm.setPWM(finger, 0, pwmValue);
  
  Serial.print("  Позиция обновлена: ");
  Serial.print(angle);
  Serial.print("° -> PWM: ");
  Serial.println(pwmValue);
}

/**
 * Устанавливает позицию ДЛЯ ВСЕХ пальцев (кулак/ладонь)
 * angle: 0-180 градусов
 */
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

/**
 * Плавное движение пальца от текущей позиции к целевой
 * finger: 0-4
 * targetAngle: целевой угол (0-180)
 * speed: скорость (мс между шагами, меньше = быстрее)
 */
void moveFingerSmooth(uint8_t finger, uint8_t targetAngle, uint16_t speed = 30) {
  if (finger > 4 || targetAngle > 180) return;
  
  uint8_t currentAngle = fingerPosition[finger];
  
  if (currentAngle < targetAngle) {
    // Движение вперёд
    for (uint8_t angle = currentAngle; angle <= targetAngle; angle++) {
      setFinger(finger, angle);
      delay(speed);
    }
  } 
  else if (currentAngle > targetAngle) {
    // Движение назад
    for (uint8_t angle = currentAngle; angle >= targetAngle; angle--) {
      setFinger(finger, angle);
      delay(speed);
    }
  }
}

/**
 * Возвращает текущую позицию пальца
 */
uint8_t getFingerPosition(uint8_t finger) {
  if (finger < 5) return fingerPosition[finger];
  return 255; // Ошибка
}

// ========== ТЕСТОВЫЕ ФУНКЦИИ (для отладки) ==========

/**
 * Тестирует все пальцы поочередно
 */
void testAllFingers() {
  Serial.println("\n[TEST] Начинаю тестирование пальцев...");
  
  const char* fingerNames[] = {"БОЛЬШОЙ", "УКАЗАТЕЛЬНЫЙ", "СРЕДНИЙ", "БЕЗЫМЯННЫЙ", "МИЗИНЕЦ"};
  
  for (int f = 0; f < 5; f++) {
    Serial.print("[TEST] Тестирую палец: ");
    Serial.println(fingerNames[f]);
    
    // Разогнуть
    setFinger(f, 0);
    delay(500);
    
    // Согнуть
    setFinger(f, 180);
    delay(500);
    
    // Вернуть в исходное
    setFinger(f, 0);
    delay(300);
  }
  
  Serial.println("[TEST] Тестирование завершено!\n");
}

/**
 * Тест волны по пальцам
 */
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
