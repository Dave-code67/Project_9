#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#define ARDUINO_I2C_ADDR 8
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// Пины подключения периферии (при сборке можно изменить на свои)
const uint8_t HC_TRIG_PIN = 2;
const uint8_t HC_ECHO_PIN = 3;
const uint8_t EMG_ANALOG_PIN = A0;

// Константы ШИМ для сервоприводов MG996R (при частоте 60 Гц)
#define SERVO_MIN 150 // 0 градусов (полностью разомкнут)
#define SERVO_MAX 600 // 180 градусов (полностью согнут)

// Настройки автоматики режимов
const uint16_t HC_MAX_CM = 12;       // Дистанция срабатывания хвата (12 см)
const int EMG_THRESHOLD = 520;       // Порог активации мышцы ЭМГ (калибруется под себя)

// Переменные состояния системы
volatile uint8_t activeMode = 0;     // Активный режим (0-Конструктор, 1-Голос, 2-Сонар, 3-ЭМГ)
volatile uint8_t requestedSensor = 0; // Какой датчик запросил ESP32

// Массивы углов для плавного хода (неблокирующая многозадачность)
uint8_t currentAngles[5] = {0, 0, 0, 0, 0};
uint8_t targetAngles[5]  = {0, 0, 0, 0, 0};
unsigned long lastServoUpdateTime = 0;
const uint8_t SERVO_SPEED_DELAY = 4; // Задержка шага в мс (меньше = быстрее)

// Глобальные переменные датчиков (обновляются асинхронно в loop)
uint16_t currentDistanceCm = 0;
uint16_t currentEmgRaw = 0;

// Переменные таймера удержания для Режима 3 (HC-SR04)
bool objectCaptured = false;
unsigned long captureTimestamp = 0;
uint16_t remainingTimerSeconds = 0;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n\n[ARDUINO] Запуск исполнительного ядра..."));

  // Настройка пинов ультразвукового датчика
  pinMode(HC_TRIG_PIN, OUTPUT);
  pinMode(HC_ECHO_PIN, INPUT);
  digitalWrite(HC_TRIG_PIN, LOW);

  // Настройка шины I2C в режиме Slave
  Wire.begin(ARDUINO_I2C_ADDR);
  Wire.onReceive(handleI2CReceive);
  Wire.onRequest(onUnoRequest);

  // Старт ШИМ-драйвера PCA9685
  if (!pwm.begin()) {
    Serial.println(F("[ERROR] Драйвер PCA9685 не обнаружен на шине I2C!"));
    while (1) delay(50);
  }
  pwm.setPWMFreq(60);

  // Начальный сброс — раскрыть ладонь
  setAllFingersDirect(0);
  Serial.println(F("[SYSTEM] Калибровка завершена. Ожидание сигналов Master..."));
}

void loop() {
  // Асинхронное фоновое чтение датчиков (без блокировки прерываний)
  measureDistanceAsync();
  currentEmgRaw = analogRead(EMG_ANALOG_PIN);

  // Реализация логики автономных режимов работы
  if (activeMode == 2) { 
    handleSonarAutomation(); // Автономный хват датчиком сонара
  } else if (activeMode == 3) { 
    handleEmgAutomation();   // Прямой био-хват от мышцы
  }

  // Неблокирующий инкремент шагов для плавной доводки сервоприводов
  if (millis() - lastServoUpdateTime >= SERVO_SPEED_DELAY) {
    lastServoUpdateTime = millis();
    updateServoPositionsAsync();
  }
}

// Измерение расстояния сонаром (вынесено из прерывания I2C, чтобы не вешать шину)
void measureDistanceAsync() {
  digitalWrite(HC_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(HC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(HC_TRIG_PIN, LOW);

  unsigned long duration = pulseIn(HC_ECHO_PIN, HIGH, 25000UL); // Ограничение тайм-аута в 25 мс
  if (duration == 0) {
    currentDistanceCm = 255; // Объект вне зоны видимости
  } else {
    currentDistanceCm = (duration * 0.0343f) / 2.0f;
    if (currentDistanceCm > 255) currentDistanceCm = 255;
  }
}

// Автоматика режима 3: Поднеси объект -> Сожми -> Подержи 3 сек -> Отпусти автоматически
void handleSonarAutomation() {
  if (currentDistanceCm <= HC_MAX_CM && currentDistanceCm > 0) {
    if (!objectCaptured) {
      objectCaptured = true;
      captureTimestamp = millis();
      setAllFingersTarget(180); // Даем команду на плавное сжатие в кулак
      Serial.println(F("[AUTOMATION] Объект в зоне захвата. Хват кисти!"));
    }
    
    // Рассчитываем оставшееся время удержания для отправки на сайт (исправлено на L)
    long elapsed = millis() - captureTimestamp;
    long remaining = 3000L - elapsed;
    if (remaining < 0) remaining = 0;
    remainingTimerSeconds = (uint16_t)(remaining / 1000L);
    
    // По истечении 3 секунд автоматически разжимаем руку, даже если объект все еще перед ней
    if (elapsed >= 3000UL && remainingTimerSeconds == 0) {
      setAllFingersTarget(0); // Плавное раскрытие ладони
    }
  } else {
    // Если объект убрали раньше времени, или цикл удержания завершен
    if (objectCaptured && (millis() - captureTimestamp >= 3000UL)) {
      objectCaptured = false; 
      remainingTimerSeconds = 0;
    } else if (!objectCaptured) {
      setAllFingersTarget(0);
      remainingTimerSeconds = 0;
    }
  }
}

// Автоматика режима 4: Пропорциональное ЭМГ управление всей кистью
void handleEmgAutomation() {
  if (currentEmgRaw >= EMG_THRESHOLD) {
    setAllFingersTarget(180);
  } else {
    setAllFingersTarget(0);
  }
}

// Неблокирующее пошаговое изменение углов сервоприводов (Генератор плавности)
void updateServoPositionsAsync() {
  for (uint8_t i = 0; i < 5; i++) {
    if (currentAngles[i] < targetAngles[i]) {
      currentAngles[i]++;
      writeToPca(i, currentAngles[i]);
    } else if (currentAngles[i] > targetAngles[i]) {
      currentAngles[i]--;
      writeToPca(i, currentAngles[i]);
    }
  }
}

// Физическая отправка импульса в канал PCA9685
void writeToPca(uint8_t channel, uint8_t angle) {
  uint16_t pwmValue = map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
  pwm.setPWM(channel, 0, pwmValue);
}

// Установка целевых углов для плавного перемещения
void setAllFingersTarget(uint8_t angle) {
  for (int i = 0; i < 5; i++) targetAngles[i] = angle;
}

// Мгновенное жесткое позиционирование (используется только при старте калибровки)
void setAllFingersDirect(uint8_t angle) {
  for (int i = 0; i < 5; i++) {
    currentAngles[i] = angle;
    targetAngles[i] = angle;
    writeToPca(i, angle);
  }
}

// Прерывание чтения шины I2C: Сюда приходят команды от ESP32
void handleI2CReceive(int byteCount) {
  if (byteCount < 2) return;

  uint8_t marker = Wire.read(); // Палец (1-5), Все (0), Служебный маркер режима (255) или Чтение датчика (254)
  uint8_t value = Wire.read();  // Угол (0-180) или номер запрашиваемого датчика / режима

  if (marker == 255) { // Служебная смена глобального режима работы
    activeMode = value;
    objectCaptured = false; // Сброс триггеров сонара
    setAllFingersTarget(0);  // Раскрыть руку при смене режима
    Serial.print(F("[I2C MASTER] Переключение режима на: "));
    Serial.println(activeMode);
    return;
  }

  if (marker == 254) { // Сигнал подготовки данных для отправки телеметрии
    requestedSensor = value;
    return;
  }

  if (marker == 0) { // Команда "Все пальцы одновременно" (например, от голоса)
    setAllFingersTarget(value);
  } else if (marker >= 1 && marker <= 5) { // Команда на конкретный палец (от Конструктора)
    targetAngles[marker - 1] = value;
  }
}

// Прерывание отправки по I2C: Вызывается мгновенно, когда ESP32 делает запрос requestFrom()
void onUnoRequest() {
  if (requestedSensor == 1) { // Запрос расстояния
    uint8_t dist = (uint8_t)currentDistanceCm;
    Wire.write(dist);
    Wire.write(0); // Заглушка второго байта
  } 
  else if (requestedSensor == 2) { // Запрос сырого ЭМГ (uint16_t разбиваем на два байта)
    Wire.write((byte)(currentEmgRaw >> 8));
    Wire.write((byte)(currentEmgRaw & 0xFF));
  } 
  else if (requestedSensor == 3) { // Запрос секунд таймера сонара
    Wire.write((byte)(remainingTimerSeconds >> 8));
    Wire.write((byte)(remainingTimerSeconds & 0xFF));
  }
  else {
    Wire.write(0);
    Wire.write(0);
  }
}
