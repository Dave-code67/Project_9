#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>

#define ARDUINO_UNO_I2C_ADDR 8

const char* ssid = "InMoov_Hand_AP";
const char* password = "12345678password";

IPAddress apIP(10, 10, 10, 1);
IPAddress netMask(255, 255, 255, 0);

const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

// Храним текущее состояние пальцев: 0-Большой, 1-Указательный, 2-Средний, 3-Безымянный, 4-Мизинец
// Значения: 0-180 (градусы)
int currentFingers[5] = {0, 0, 0, 0, 0};

// Текущий режим: 0-конструктор, 1-БИО-ЭМГ, 2-автохват, 3-голос
int currentMode = 0;

// Защита «Один хозяин»
bool isRobotBusy = false;
unsigned long macroStartTime = 0;
unsigned long macroDuration = 0;

// ===== ОТПРАВКА КОМАНД НА ARDUINO =====
void sendCommandToUno(byte fingerNum, byte angle) {
  Wire.beginTransmission(ARDUINO_UNO_I2C_ADDR);
  Wire.write(fingerNum); // 0-кулак, 1-5 для конкретного пальца
  Wire.write(angle);     // 0-180 градусов
  Wire.endTransmission();
  
  Serial.print("[I2C] Sent: Finger ");
  Serial.print(fingerNum);
  Serial.print(" Angle ");
  Serial.println(angle);
}

// ===== ЛОГИКА ЗАЩИТЫ ОТ ФАКА =====

/**
 * Проверка валидности конфигурации пальцев
 * Возвращает: true если конфиг валиден, false если это запрещённое положение
 */
bool isValidFingerConfig(int fingers[5]) {
  // fingers[0] = большой, fingers[1] = указательный, fingers[2] = средний, 
  // fingers[3] = безымянный, fingers[4] = мизинец
  
  int thumb = fingers[0];
  int index = fingers[1];
  int middle = fingers[2];
  int ring = fingers[3];
  int pinky = fingers[4];
  
  // ЗАПРЕЩЁННОЕ ПОЛОЖЕНИЕ 1: Отогнут ТОЛЬКО средний (все остальные согнуты)
  if (middle > 90 && thumb < 90 && index < 90 && ring < 90 && pinky < 90) {
    return false;
  }
  
  // ЗАПРЕЩЁН��ОЕ ПОЛОЖЕНИЕ 2: Отогнуты ТОЛЬКО средний + большой (остальные согнуты)
  if (middle > 90 && thumb > 90 && index < 90 && ring < 90 && pinky < 90) {
    return false;
  }
  
  return true;
}

/**
 * Проверка возможности изменить палец в режимах 0 (конструктор) и 3 (голос)
 * Возвращает: true если движение допустимо, false если нарушает защиту
 */
bool canMoveFingerSafeMode(int fingerIdx, int targetAngle, int currentState[5]) {
  int thumb = currentState[0];
  int index = currentState[1];
  int middle = currentState[2];
  int ring = currentState[3];
  int pinky = currentState[4];
  
  // Создаём временное состояние с новым углом
  int tempFingers[5];
  for(int i = 0; i < 5; i++) tempFingers[i] = currentState[i];
  tempFingers[fingerIdx] = targetAngle;
  
  // Проверяем, не приведёт ли это к запрещённому положению
  return isValidFingerConfig(tempFingers);
}

/**
 * Дополнительная проверка для режима конструктора (режим 0)
 * Правило 1: При полностью сжатой руке (все 180°)
 *   - Нельзя отогнуть ТОЛЬКО средний
 *   - Нельзя отогнуть ТОЛЬКО средний + большой
 *   - Если отогнуты ≥2 других пальца, средний можно отогнуть
 * 
 * Правило 2: При разогнутой руке (все 0°)
 *   - Нельзя согнуть все пальцы кроме среднего
 *   - Нельзя согнуть указательный/безымянный/мизинец оставляя средний + большой разогнутыми
 */
bool isValidConstructorMove(int fingerIdx, int targetAngle, int currentState[5]) {
  int thumb = currentState[0];
  int index = currentState[1];
  int middle = currentState[2];
  int ring = currentState[3];
  int pinky = currentState[4];
  
  // Временное состояние
  int tempFingers[5];
  for(int i = 0; i < 5; i++) tempFingers[i] = currentState[i];
  tempFingers[fingerIdx] = targetAngle;
  
  // ===== ПРАВИЛО 1: При полностью сжатой руке (все согнуты) =====
  // Проверяем, что все текущие пальцы согнуты (>90 градусов)
  bool allClenched = (thumb > 90 && index > 90 && middle > 90 && ring > 90 && pinky > 90);
  
  if (allClenched && targetAngle <= 90) {
    // Пытаемся отогнуть какой-то палец
    int openCount = 0;
    int openFingers[5] = {0};
    
    for(int i = 0; i < 5; i++) {
      if (tempFingers[i] <= 90) {
        openFingers[openCount++] = i;
      }
    }
    
    // Запрещаем: отогнуть только средний
    if (openCount == 1 && openFingers[0] == 2) {
      return false;
    }
    
    // Запрещаем: отогнуть только средний + большой
    if (openCount == 2 && 
        ((openFingers[0] == 0 && openFingers[1] == 2) || 
         (openFingers[0] == 2 && openFingers[1] == 0))) {
      return false;
    }
  }
  
  // ===== ПРАВИЛО 2: При разогнутой руке (все разогнуты) =====
  // Проверяем, что все текущие пальцы разогнуты (<90 градусов)
  bool allOpen = (thumb < 90 && index < 90 && middle < 90 && ring < 90 && pinky < 90);
  
  if (allOpen && targetAngle > 90) {
    // Пытаемся согнуть какой-то палец
    int clenchedCount = 0;
    int clenchedFingers[5] = {0};
    
    for(int i = 0; i < 5; i++) {
      if (tempFingers[i] > 90) {
        clenchedFingers[clenchedCount++] = i;
      }
    }
    
    // Запрещаем: согнуть все пальцы кроме среднего
    if (clenchedCount == 4 && tempFingers[2] < 90) {
      // Проверяем, что согнуты все КРОМЕ среднего
      if ((tempFingers[0] > 90 && tempFingers[1] > 90 && 
           tempFingers[3] > 90 && tempFingers[4] > 90) && 
          tempFingers[2] < 90) {
        return false;
      }
    }
    
    // Запрещаем: согнуть указ/безым/миз оставляя средний + большой разогнутыми
    if ((fingerIdx == 1 || fingerIdx == 3 || fingerIdx == 4) && targetAngle > 90) {
      // Проверяем, что средний и большой остаются разогнутыми
      if (tempFingers[2] < 90 && tempFingers[0] < 90) {
        return false;
      }
    }
  }
  
  return true;
}

// ===== ВЕunderlying HTML ИНТЕРФЕЙС =====
void handleRoot() {
  String html = "<!DOCTYPE html><html lang='ru'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>InMoov i2 OS - Hotline Cyberpunk</title>";
  html += "<style>";
  html += ":root { --neon: #00ffcc; --bg: #05050a; --panel: #0d1117; --danger: #ff007f; --grid: rgba(255, 0, 127, 0.05); }";
  html += "body { font-family: 'Courier New', monospace; background: var(--bg); background-image: linear-gradient(var(--grid) 1px, transparent 1px), linear-gradient(90deg, var(--grid) 1px, transparent 1px); background-size: 20px 20px; color: #e2e8f0; padding: 20px; min-height: 100vh; }";
  html += ".container { width: 95%; max-width: 500px; padding: 30px; background: rgba(15, 15, 30, 0.9); backdrop-filter: blur(20px); border: 2px solid var(--danger); border-radius: 4px; box-shadow: 0 0 30px rgba(255, 0, 127, 0.2); margin: 0 auto; }";
  html += "h2 { text-transform: uppercase; letter-spacing: 5px; font-size: 1.1em; color: #fff; margin-bottom: 25px; text-shadow: 0 0 10px var(--danger); text-align: center; }";
  html += ".mode-select { background: #000; border: 2px solid var(--neon); color: var(--neon); padding: 12px; width: 100%; margin-bottom: 20px; font-family: 'Courier New', monospace; font-weight: bold; text-transform: uppercase; }";
  html += ".constructor-zone { background: rgba(0, 0, 0, 0.7); border: 1px solid var(--danger); min-height: 110px; margin-bottom: 25px; padding: 15px; border-radius: 4px; display: flex; flex-wrap: wrap; gap: 8px; }";
  html += ".algo-block { background: rgba(0, 255, 204, 0.05); color: var(--neon); padding: 8px 12px; border: 1px solid var(--neon); border-radius: 2px; font-size: 10px; font-weight: bold; cursor: pointer; transition: 0.2s; text-transform: uppercase; }";
  html += ".algo-block:hover { background: var(--danger); color: #fff; border-color: var(--danger); box-shadow: 0 0 10px var(--danger); }";
  html += ".block-wait { border-color: #ffff00; color: #ffff00; background: rgba(255, 255, 0, 0.03); }";
  html += ".controls { margin-bottom: 20px; text-align: left; }";
  html += "label { font-size: 11px; color: #00ffcc; text-transform: uppercase; letter-spacing: 1.5px; font-weight: bold; display: block; margin-bottom: 5px; text-shadow: 0 0 3px var(--neon); }";
  html += ".val-display { float: right; color: #ffff00; font-size: 1em; text-shadow: 0 0 5px #ffff00; }";
  html += "input[type=range] { -webkit-appearance: none; width: 100%; background: #111; height: 8px; border-radius: 2px; border: 1px solid var(--danger); margin: 10px 0; }";
  html += "input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; height: 20px; width: 20px; border-radius: 50%; background: var(--danger); cursor: pointer; box-shadow: 0 0 10px var(--danger); }";
  html += ".btn-add { width: 100%; background: transparent; color: var(--neon); border: 1px solid var(--neon); padding: 12px; font-weight: bold; cursor: pointer; margin-top: 10px; text-transform: uppercase; font-size: 10px; font-family: 'Courier New', monospace; transition: 0.3s; }";
  html += ".btn-add:hover { background: var(--neon); color: #000; box-shadow: 0 0 15px var(--neon); }";
  html += ".btn-run { width: 100%; background: var(--danger); color: #fff; padding: 18px; border: none; font-size: 1.1em; font-weight: bold; cursor: pointer; margin-top: 15px; text-transform: uppercase; letter-spacing: 2px; font-family: 'Courier New', monospace; transition: 0.3s; }";
  html += ".btn-run:hover { background: #ff3399; box-shadow: 0 0 30px var(--danger); }";
  html += ".btn-clear { background: none; border: none; color: #555; cursor: pointer; margin-top: 15px; font-size: 0.8em; width: 100%; text-transform: uppercase; letter-spacing: 1px; font-family: 'Courier New', monospace; }";
  html += ".btn-clear:hover { color: var(--danger); }";
  html += ".status-msg { padding: 10px; background: rgba(255, 0, 127, 0.1); border-left: 3px solid var(--danger); margin-bottom: 15px; color: #fff; font-size: 0.9em; display: none; }";
  html += ".status-msg.show { display: block; }";
  html += ".disabled-ui { opacity: 0.3; pointer-events: none; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h2>InMoov Cyber System</h2>";
  
  // Селектор режимов
  html += "<select id='modeSelect' class='mode-select' onchange='switchMode(this.value)'>";
  html += "<option value='0'>[ 01 // РУЧНОЙ КОНСТРУКТОР ]</option>";
  html += "<option value='1'>[ 02 // БИО-ЭМГ ИНТЕРФЕЙС ]</option>";
  html += "<option value='2'>[ 03 // АВТОНОМНЫЙ ХВАТ ]</option>";
  html += "<option value='3'>[ 04 // ГОЛОСОВОЙ МОДУЛЬ ]</option>";
  html += "</select>";
  
  html += "<div id='statusMsg' class='status-msg'></div>";
  
  // Конструктор (режим 0)
  html += "<div id='constructorMode'>";
  html += "<div class='constructor-zone' id='queue'>";
  html += "<div id='placeholder' style='color: #333; width: 100%; margin-top: 35px; font-size: 10px; letter-spacing: 2px; text-align: center;'>ОЖИДАНИЕ КОМАНД...</div>";
  html += "</div>";
  
  html += "<div class='controls'>";
  html += "<label>ВЫБОР ПРИВОДА: <span id='f-val' class='val-display'>БОЛЬШОЙ ПАЛЕЦ</span></label>";
  html += "<input type='range' id='f-slider' min='0' max='5' step='1' value='0' oninput='updateF()'>";
  html += "</div>";
  
  html += "<div class='controls'>";
  html += "<label>УГОЛ СЖАТИЯ: <span id='p-val' class='val-display'>0°</span></label>";
  html += "<input type='range' id='p-slider' min='0' max='180' step='5' value='0' oninput='updateP()'>";
  html += "<button class='btn-add' onclick='addFingerAction()'>ДОБАВИТЬ В ОЧЕРЕДЬ</button>";
  html += "</div>";
  
  html += "<div class='controls' style='margin-top: 20px;'>";
  html += "<label>ЗАДЕРЖКА: <span id='w-val' class='val-display'>1 СЕК</span></label>";
  html += "<input type='range' id='w-slider' min='1' max='20' step='1' value='1' oninput='updateW()'>";
  html += "<button class='btn-add' style='border-color: #ffff00; color: #ffff00;' onclick='addWaitAction()'>ДОБАВИТЬ ПАУЗУ</button>";
  html += "</div>";
  
  html += "<button class='btn-run' onclick='runAlgorithm()' id='run-btn'>ИСПОЛНИТЬ ЦИКЛ</button>";
  html += "<button class='btn-clear' onclick='clearQueue()'>ПОЛНЫЙ СБРОС</button>";
  html += "</div>";
  
  // Голосовой режим (режим 3)
  html += "<div id='voiceMode' class='disabled-ui' style='display: none;'>";
  html += "<button class='btn-run' style='width: 100%; margin-top: 20px;' onclick='startVoiceControl()'>🎙️ НАЧАТЬ ГОЛОСОВОЙ ВВОД</button>";
  html += "</div>";
  
  // БИО-ЭМГ (режим 1)
  html += "<div id='bioMode' class='disabled-ui' style='display: none;'>";
  html += "<p style='text-align: center; color: #888; padding: 40px 0;'>БИО-ЭМГ ИНТЕРФЕЙС (в разработке)</p>";
  html += "</div>";
  
  // Автохват (режим 2)
  html += "<div id='autoMode' class='disabled-ui' style='display: none;'>";
  html += "<p style='text-align: center; color: #888; padding: 40px 0;'>АВТОНОМНЫЙ ХВАТ (активен)</p>";
  html += "</div>";
  
  html += "</div>";
  
  // Скрипты
  html += "<script>";
  html += "let program = []; let isBusy = false; let currentMode = 0;";
  html += "let currentFingers = [0, 0, 0, 0, 0];";
  html += "const fingers = ['БОЛЬШОЙ ПАЛЕЦ', 'УКАЗАТЕЛЬНЫЙ', 'СРЕДНИЙ ПАЛЕЦ', 'БЕЗЫМЯННЫЙ', 'МИЗИНЕЦ', 'ВСЯ КИСТЬ'];";
  html += "const audioCtx = new (window.AudioContext || window.webkitAudioContext)();";
  
  html += "function playSound(type) {";
  html += "  const osc = audioCtx.createOscillator(); const gain = audioCtx.createGain();";
  html += "  osc.connect(gain); gain.connect(audioCtx.destination);";
  html += "  if(type==='click'){ osc.type='square'; osc.frequency.setValueAtTime(600, audioCtx.currentTime); osc.frequency.exponentialRampToValueAtTime(150, audioCtx.currentTime+0.08); gain.gain.setValueAtTime(0.1, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime+0.08); }";
  html += "  else if(type==='error'){ osc.type='sawtooth'; osc.frequency.setValueAtTime(150, audioCtx.currentTime); osc.frequency.linearRampToValueAtTime(80, audioCtx.currentTime+0.2); gain.gain.setValueAtTime(0.15, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0, audioCtx.currentTime+0.2); }";
  html += "  else if(type==='run'){ osc.type='sawtooth'; osc.frequency.setValueAtTime(120, audioCtx.currentTime); osc.frequency.linearRampToValueAtTime(700, audioCtx.currentTime+0.25); gain.gain.setValueAtTime(0.1, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0, audioCtx.currentTime+0.25); }";
  html += "  osc.start(); osc.stop(audioCtx.currentTime + (type==='click' ? 0.08 : type==='error' ? 0.2 : 0.25));";
  html += "}";
  
  html += "function updateF() { playSound('click'); document.getElementById('f-val').innerText = fingers[document.getElementById('f-slider').value]; }";
  html += "function updateP() { playSound('click'); document.getElementById('p-val').innerText = document.getElementById('p-slider').value + '°'; }";
  html += "function updateW() { playSound('click'); document.getElementById('w-val').innerText = document.getElementById('w-slider').value + ' СЕК'; }";
  
  html += "function switchMode(mode) {";
  html += "  currentMode = parseInt(mode);";
  html += "  fetch('/set_mode?m=' + mode);";
  html += "  document.getElementById('constructorMode').style.display = mode === '0' ? 'block' : 'none';";
  html += "  document.getElementById('bioMode').style.display = mode === '1' ? 'block' : 'none';";
  html += "  document.getElementById('autoMode').style.display = mode === '2' ? 'block' : 'none';";
  html += "  document.getElementById('voiceMode').style.display = mode === '3' ? 'block' : 'none';";
  html += "}";
  
  html += "function addFingerAction() {";
  html += "  playSound('click');";
  html += "  let f_idx = parseInt(document.getElementById('f-slider').value);";
  html += "  let angle = parseInt(document.getElementById('p-slider').value);";
  html += "  document.getElementById('placeholder').style.display = 'none';";
  html += "  let id = Date.now();";
  html += "  program.push({id, type: 'motor', finger: f_idx, angle: angle});";
  html += "  updateUI(fingers[f_idx] + ' : ' + angle + '°', 'algo-block', id);";
  html += "}";
  
  html += "function addWaitAction() {";
  html += "  playSound('click');";
  html += "  let sec = document.getElementById('w-slider').value;";
  html += "  document.getElementById('placeholder').style.display = 'none';";
  html += "  let id = Date.now();";
  html += "  program.push({id, type: 'wait', val: sec});";
  html += "  updateUI('ПАУЗА ' + sec + ' СЕК', 'algo-block block-wait', id);";
  html += "}";
  
  html += "function updateUI(text, className, id) {";
  html += "  let div = document.createElement('div');";
  html += "  div.className = className;";
  html += "  div.innerText = text;";
  html += "  div.onclick = () => { playSound('click'); program = program.filter(i => i.id !== id); div.remove(); if(!program.length) document.getElementById('placeholder').style.display = 'block'; };";
  html += "  document.getElementById('queue').appendChild(div);";
  html += "}";
  
  html += "function clearQueue() { playSound('click'); program = []; document.getElementById('queue').innerHTML = '<div id=\"placeholder\" style=\"color: #333; width: 100%; margin-top: 35px; font-size: 10px; letter-spacing: 2px; text-align: center;\">ОЖИДАНИЕ КОМАНД...</div>'; }";
  
  html += "function showStatus(msg, isError) {";
  html += "  const el = document.getElementById('statusMsg');";
  html += "  el.innerText = msg;";
  html += "  el.classList.add('show');";
  html += "  if(isError) playSound('error');";
  html += "  setTimeout(() => el.classList.remove('show'), 3000);";
  html += "}";
  
  html += "async function runAlgorithm() {";
  html += "  if(isBusy || !program.length) return;";
  html += "  isBusy = true;";
  html += "  playSound('run');";
  html += "  const btn = document.getElementById('run-btn');";
  html += "  btn.innerText = 'ВЫПОЛНЕНИЕ...';";
  html += "  btn.style.background = '#222';";
  html += "  for (let cmd of program) {";
  html += "    if (cmd.type === 'motor') {";
  html += "      let res = await fetch(`/execute?f=${cmd.finger}&a=${cmd.angle}`);";
  html += "      let status = await res.text();";
  html += "      if(status.includes('ERROR')) { showStatus('❌ ' + status, true); }";
  html += "      await new Promise(r => setTimeout(r, 500));";
  html += "    } else if (cmd.type === 'wait') {";
  html += "      await new Promise(r => setTimeout(r, cmd.val * 1000));";
  html += "    }";
  html += "  }";
  html += "  isBusy = false;";
  html += "  btn.innerText = 'ИСПОЛНИТЬ ЦИКЛ';";
  html += "  btn.style.background = 'var(--danger)';";
  html += "  clearQueue();";
  html += "}";
  
  html += "function startVoiceControl() {";
  html += "  window.SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;";
  html += "  if (!window.SpeechRecognition) { showStatus('❌ Браузер не поддерживает голос', true); return; }";
  html += "  const recognition = new SpeechRecognition();";
  html += "  recognition.lang = 'ru-RU';";
  html += "  recognition.start();";
  html += "  const btn = document.querySelector('[onclick=\"startVoiceControl()\"]');";
  html += "  btn.innerText = '⏳ СЛУШАЮ...';";
  html += "  recognition.onresult = function(event) {";
  html += "    const cmd = event.results[0].transcript.toLowerCase();";
  html += "    btn.innerText = '✓ ' + cmd.toUpperCase();";
  html += "    if (cmd.includes('кулак') || cmd.includes('сожми')) {";
  html += "      for(let i=0; i<5; i++) fetch(`/execute?f=${i}&a=180`);";
  html += "    } else if (cmd.includes('ладонь') || cmd.includes('открой')) {";
  html += "      for(let i=0; i<5; i++) fetch(`/execute?f=${i}&a=0`);";
  html += "    }";
  html += "    setTimeout(() => btn.innerText = '🎙️ НАЧАТЬ ГОЛОСОВОЙ ВВОД', 2000);";
  html += "  };";
  html += "  recognition.onerror = () => { showStatus('❌ Ошибка голоса', true); };";
  html += "}";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSetMode() {
  if (server.hasArg("m")) {
    currentMode = server.arg("m").toInt();
    Serial.print("[MODE] Switched to mode: ");
    Serial.println(currentMode);
  }
  server.send(200, "text/plain", "OK");
}

void handleExecute() {
  if (isRobotBusy) {
    if (millis() - macroStartTime < macroDuration) {
      server.send(200, "text/plain", "BUSY");
      return;
    } else {
      isRobotBusy = false;
    }
  }

  int f = server.hasArg("f") ? server.arg("f").toInt() : -1;
  int a = server.hasArg("a") ? server.arg("a").toInt() : 0;

  // Защита диапазона
  if (a < 0 || a > 180 || f < 0 || f > 5) {
    server.send(200, "text/plain", "ERROR: Invalid params");
    return;
  }

  // Создаём временное состояние
  int tempFingers[5];
  for(int i = 0; i < 5; i++) tempFingers[i] = currentFingers[i];
  
  if (f == 5) {
    // Сигнал на всю кисть
    for(int i = 0; i < 5; i++) tempFingers[i] = a;
  } else {
    // Сигнал на конкретный палец
    tempFingers[f] = a;
  }

  // ===== ЛОГИКА ЗАЩИТЫ В ЗАВИСИМОСТИ ОТ РЕЖИМА =====
  bool isAllowed = true;
  
  if (currentMode == 0 || currentMode == 3) {
    // Режим конструктора (0) и голосовой (3) - ПОЛНАЯ ЗАЩИТА
    
    // Сначала базовая проверка запрещённых положений
    if (!isValidFingerConfig(tempFingers)) {
      server.send(200, "text/plain", "ERROR: Forbidden position");
      Serial.println("[SAFETY] Forbidden finger position blocked");
      return;
    }
    
    // Затем проверка конструктора
    if (!isValidConstructorMove(f, a, currentFingers)) {
      server.send(200, "text/plain", "ERROR: Invalid move (safety)");
      Serial.println("[SAFETY] Invalid constructor move blocked");
      return;
    }
    
  } else if (currentMode == 1) {
    // БИО-ЭМГ (режим 1) - ПОЛНАЯ ЗАЩИТА (как и конструктор)
    
    if (!isValidFingerConfig(tempFingers)) {
      server.send(200, "text/plain", "ERROR: Forbidden position");
      return;
    }
    
    if (!isValidConstructorMove(f, a, currentFingers)) {
      server.send(200, "text/plain", "ERROR: Invalid move (safety)");
      return;
    }
    
  } else if (currentMode == 2) {
    // Автохват (режим 2) - БЕЗ ОГРАНИЧЕНИЙ
    isAllowed = true;
  }

  // Если все проверки пройдены
  isRobotBusy = true;
  macroStartTime = millis();
  macroDuration = 500;

  // Обновляем состояние
  for(int i = 0; i < 5; i++) currentFingers[i] = tempFingers[i];

  // Отправляем команды на Arduino
  if (f == 5) {
    // Вся кисть
    for(int i = 0; i < 5; i++) {
      sendCommandToUno(i + 1, a);
      delay(30);
    }
  } else {
    // Конкретный палец (f+1, потому что Arduino ждёт 1-5)
    sendCommandToUno(f + 1, a);
  }

  server.send(200, "text/plain", "OK");
}

void handleNotFound() {
  server.sendHeader("Location", "http://10.10.10", true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n[ESP32] Initializing...");
  
  Wire.begin(21, 22);
  Serial.println("[I2C] Initialized on GPIO 21 (SDA), 22 (SCL)");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMask);
  WiFi.softAP(ssid, password);
  dnsServer.start(DNS_PORT, "*", apIP);
  
  Serial.print("[WiFi] AP Started: ");
  Serial.print(ssid);
  Serial.println(" | IP: 10.10.10.1");
  
  server.on("/", handleRoot);
  server.on("/set_mode", handleSetMode);
  server.on("/execute", handleExecute);
  server.on("/generate_204", handleRoot);
  server.on("/generate204", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  
  Serial.println("[ESP32] Ready!");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(10);
}
