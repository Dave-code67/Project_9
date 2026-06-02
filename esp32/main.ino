#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>

#define ARDUINO_UNO_I2C_ADDR 8

// Настройки автономной Wi-Fi точки доступа
const char* ssid = "InMoov_Hand_AP";
const char* password = "12345678password";

IPAddress apIP(10, 10, 10, 1);
IPAddress netMask(255, 255, 255, 0);
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

// Глобальное состояние роборуки
int currentFingers[5] = {0, 0, 0, 0, 0};
int currentMode = 0;
bool isRobotBusy = false;          // Защита от одновременных запросов пользователей
unsigned long robotBusyUntil = 0;  // Таймер блокировки коллизий

// Функция отправки углов на Arduino Uno по шине I2C
void sendCommandToUno(byte fingerNum, byte angle) {
  Wire.beginTransmission(ARDUINO_UNO_I2C_ADDR);
  Wire.write(fingerNum);
  Wire.write(angle);
  Wire.endTransmission();
  Serial.printf("[I2C] Отправлено: Палец %d, Угол %d\n", fingerNum, angle);
}

// Функция отправки текущего режима работы на Arduino Uno
void sendModeToUno(int mode) {
  Wire.beginTransmission(ARDUINO_UNO_I2C_ADDR);
  Wire.write((byte)255); // Маркер смены режима
  Wire.write((byte)mode);
  Wire.endTransmission();
  Serial.printf("[I2C] Изменен режим на UNO: %d\n", mode);
}

// Проверка базовой анатомической безопасности (Anti-Jam System)
bool isValidFingerConfig(int fingers[5]) {
  int thumb = fingers[0];
  int index = fingers[1];
  int middle = fingers[2];
  int ring = fingers[3];
  int pinky = fingers[4];

  // Запрещено: поднят только средний палец
  if (middle > 90 && thumb < 90 && index < 90 && ring < 90 && pinky < 90) return false;
  // Запрещено: подняты только средний и большой
  if (middle > 90 && thumb > 90 && index < 90 && ring < 90 && pinky < 90) return false;
  return true;
}

// Проверка корректности шага алгоритма в Конструкторе
bool isValidConstructorMove(int fingerIdx, int targetAngle, int currentState[5]) {
  int tempFingers[5];
  for(int i = 0; i < 5; i++) tempFingers[i] = currentState[i];
  
  if (fingerIdx == 5) { // Команда всей кисти целиком
    for(int i = 0; i < 5; i++) tempFingers[i] = targetAngle;
  } else if (fingerIdx >= 0 && fingerIdx < 5) {
    tempFingers[fingerIdx] = targetAngle;
  }

  if (!isValidFingerConfig(tempFingers)) return false;

  // Проверка динамических переходов суставов
  bool allClenched = true;
  for(int i = 0; i < 5; i++) {
    if(currentState[i] <= 90) allClenched = false;
  }

  // Из полностью сжатого кулака нельзя выпрямить только один средний палец
  if (allClenched && targetAngle <= 90) {
    if (fingerIdx == 2) return false; 
  }
  return true;
}
// Функция генерации и отправки веб-интерфейса в браузер
void handleRoot() {
  String html = "<!DOCTYPE html><html lang='ru'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>InMoov IDE // Control Terminal</title>";
  html += "<style>";
  html += ":root { --cls-blue: #00d2ff; --cls-dark-blue: #006699; --cls-bg: #020813; --cls-panel: rgba(5, 19, 43, 0.85); --cls-neon: #00ffff; --cls-text: #d1f4ff; --cls-muted: #5b7c99; --cls-alert: #ff3366; }";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; font-family: 'JetBrains Mono', 'Consolas', monospace; }";
  html += "body { background-color: var(--cls-bg); color: var(--cls-text); padding: 30px 15px; min-height: 100vh; overflow-x: hidden; position: relative; }";
  html += ".bg-grid { position: fixed; inset: 0; z-index: -1; opacity: 0.12; pointer-events: none; ";
  html += "background-image: linear-gradient(var(--cls-blue) 1px, transparent 1px), linear-gradient(90deg, var(--cls-blue) 1px, transparent 1px); background-size: 30px 30px; }";
  html += ".bg-circuit { position: fixed; inset: 0; z-index: -2; opacity: 0.05; pointer-events: none; background: radial-gradient(circle at 80% 20%, var(--cls-neon) 0%, transparent 40%), radial-gradient(circle at 20% 80%, var(--cls-dark-blue) 0%, transparent 50%); }";
  html += ".container { width: 100%; max-width: 540px; margin: 0 auto; position: relative; }";
  html += "h1 { font-size: 1.5rem; letter-spacing: 2px; color: var(--cls-blue); margin-bottom: 25px; text-transform: uppercase; text-shadow: 0 0 10px rgba(0,210,255,0.4); text-align: center; }";
  html += "#status-bar { background: #000; border-left: 4px solid var(--cls-blue); padding: 12px; font-weight: bold; margin-bottom: 20px; text-transform: uppercase; font-size: 0.85rem; letter-spacing: 1px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }";
  html += ".panel-card { background: var(--cls-panel); border: 1px solid rgba(0, 210, 255, 0.2); border-radius: 8px; padding: 20px; margin-bottom: 20px; box-shadow: 0 8px 32px rgba(0,0,0,0.4); backdrop-filter: blur(8px); }";
  html += ".section-title { font-size: 0.85rem; color: var(--cls-muted); text-transform: uppercase; letter-spacing: 2px; margin-bottom: 12px; font-weight: bold; display: flex; align-items: center; gap: 8px; }";
  html += ".section-title::after { content: ''; flex: 1; height: 1px; background: rgba(0, 210, 255, 0.15); }";
  html += ".btn-core { display: block; width: 100%; background: transparent; color: var(--cls-blue); border: 1px solid var(--cls-blue); padding: 12px; font-weight: bold; text-transform: uppercase; cursor: pointer; transition: all 0.25s ease; border-radius: 4px; letter-spacing: 1px; font-size: 0.85rem; }";
  html += ".btn-core:hover { background: rgba(0, 210, 255, 0.15); box-shadow: 0 0 12px rgba(0, 210, 255, 0.3); }";
  html += ".btn-action { background: var(--cls-blue); color: #000; border: none; font-size: 0.95rem; font-weight: bold; letter-spacing: 2px; padding: 16px; margin-top: 10px; }";
  html += ".btn-action:hover { background: var(--cls-neon); box-shadow: 0 0 20px var(--cls-neon); }";
  html += ".btn-clear { background: rgba(255,51,102,0.1); border: 1px solid var(--cls-alert); color: var(--cls-alert); margin-top: 8px; padding: 10px; }";
  html += ".btn-clear:hover { background: var(--cls-alert); color: #000; }";
  html += ".select-core { background: #010c1e; border: 1px solid var(--cls-blue); color: var(--cls-blue); padding: 12px; width: 100%; font-size: 0.9rem; font-weight: bold; text-transform: uppercase; outline: none; border-radius: 4px; }";
  html += ".constructor-zone { background: rgba(0, 4, 10, 0.6); border: 1px dashed var(--cls-muted); min-height: 90px; margin-bottom: 15px; padding: 12px; display: flex; flex-wrap: wrap; gap: 8px; align-content: flex-start; border-radius: 4px; }";
  html += ".algo-block { background: rgba(0, 210, 255, 0.1); color: var(--cls-neon); padding: 8px 12px; border: 1px solid var(--cls-blue); border-radius: 4px; font-size: 0.75rem; font-weight: bold; cursor: pointer; text-transform: uppercase; display: flex; align-items: center; gap: 6px; }";
  html += ".algo-block:hover { border-color: var(--cls-alert); color: var(--cls-alert); background: rgba(255,51,102,0.05); }";
  html += ".algo-block.block-wait { border-color: #ffaa00; color: #ffaa00; background: rgba(255,170,0,0.05); }";
  html += ".control-group { margin-bottom: 15px; }";
  html += "label { font-size: 0.75rem; color: var(--cls-muted); text-transform: uppercase; display: block; margin-bottom: 5px; }";
  html += ".val-badge { float: right; color: var(--cls-neon); font-weight: bold; }";
  html += "input[type=range] { -webkit-appearance: none; width: 100%; background: transparent; margin: 8px 0; }";
  html += "input[type=range]:focus { outline: none; }";
  html += "input[type=range]::-webkit-slider-runnable-track { width: 100%; height: 4px; background: #0c1a30; border-radius: 2px; }";
  html += "input[type=range]::-webkit-slider-thumb { height: 18px; width: 10px; border-radius: 2px; background: var(--cls-blue); cursor: pointer; -webkit-appearance: none; margin-top: -7px; box-shadow: 0 0 8px var(--cls-blue); }";
  html += ".mode-view { display: none; } .mode-view.active { display: block; }";
  html += ".info-terminal { background: rgba(0, 5, 15, 0.5); border: 1px solid rgba(0, 210, 255, 0.1); padding: 12px; border-radius: 4px; font-size: 0.8rem; line-height: 1.5; color: #a3c2db; }";
  html += ".cmd-list { margin-top: 8px; padding-left: 15px; color: var(--cls-blue); }";
  html += ".live-metrics { font-size: 1.1rem; color: var(--cls-neon); font-weight: bold; margin-top: 10px; text-align: center; letter-spacing: 1px; }";
  html += "</style></head><body>";
  html += "<div class='bg-circuit'></div><div class='bg-grid'></div>";
  html += "<div class='container'><h1>InMoov Hand Center v3.0</h1>";
  html += "<div id='status-bar' style='color:var(--cls-blue);'>> Инициализация терминала...</div>";
  html += "<div class='panel-card'>";
  html += "<div class='section-title'>Системный режим</div>";
  html += "<button id='lockBtn' class='btn-core' onclick='unlockSystemMenu()'>[ Сменить режим работы ] 🔒</button>";
  html += "<select id='modeSelect' class='select-core' style='display: none;' onchange='handleMenuSelect(this.value)'>";
  html += "<option value='0'>01 // Ручной Конструктор алгоритмов</option>";
  html += "<option value='1'>02 // Голосовое управление</option>";
  html += "<option value='2'>03 // Автономный хват датчиком HC-SR04</option>";
  html += "<option value='3'>04 // БИО-ЭМГ мышечный интерфейс</option>";
  html += "</select></div><div id='main-workspace'>";
  // РЕЖИМ 1: КОНСТРУКТОР
  html += "<div id='view-0' class='mode-view active'>";
  html += "  <div class='section-title'>Очередь алгоритма команд</div>";
  html += "  <div class='constructor-zone' id='queue'>";
  html += "    <div id='placeholder' style='color:#445577; width:100%; margin-top:25px; font-size:0.75rem; text-align:center;'>[ СИСТЕМА ГОТОВА К ВВОДУ КОМАНД ]</div>";
  html += "  </div>";
  html += "  <div class='panel-card'>";
  html += "    <div class='control-group'>";
  html += "      <label>Выбор привода: <span id='f-val' class='val-badge'>БОЛЬШОЙ ПАЛЕЦ</span></label>";
  html += "      <input type='range' id='f-slider' min='0' max='5' step='1' value='0' oninput='updateF()' />";
  html += "    </div>";
  html += "    <div class='control-group'>";
  html += "      <label>Угол сжатия: <span id='p-val' class='val-badge'>0°</span></label>";
  html += "      <input type='range' id='p-slider' min='0' max='180' step='5' value='0' oninput='updateP()' />";
  html += "    </div>";
  html += "    <button class='btn-core' onclick='addFingerAction()'>Добавить команду в скрипт</button>";
  html += "  </div>";
  html += "  <div class='panel-card'>";
  html += "    <div class='control-group'>";
  html += "      <label>Таймаут паузы: <span id='w-val' class='val-badge'>1 СЕК</span></label>";
  html += "      <input type='range' id='w-slider' min='1' max='10' step='1' value='1' oninput='updateW()' />";
  html += "    </div>";
  html += "    <button class='btn-core' style='border-color:#ffaa00; color:#ffaa00;' onclick='addWaitAction()'>Добавить задержку</button>";
  html += "  </div>";
  html += "  <button class='btn-core btn-action' id='run-btn' onclick='runAlgorithm()'>Выполнить скрипт</button>";
  html += "  <button class='btn-core btn-clear' onclick='clearQueue()'>Очистить буфер</button>";
  html += "</div>";

  // РЕЖИМ 2: ГОЛОС
  html += "<div id='view-1' class='mode-view'>";
  html += "  <div class='panel-card'>";
  html += "    <div class='info-terminal'>";
  html += "      <b>ГОЛОСОВОЙ МОДУЛЬ RECOGNITION:</b><br/>";
  html += "      Синтаксический анализатор браузера распознает речь на лету.<br/>";
  html += "      <div class='cmd-list'>";
  html += "        • «кулак» / «сожми» → Полный хват кисти (180°)<br/>";
  html += "        • «ладонь» / «открой» → Полное разжатие (0°)";
  html += "      </div>";
  html += "    </div>";
  html += "    <div style='margin-top:20px;'>";
  html += "      <button id='voiceBtn' class='btn-core' style='border-color:var(--cls-neon); color:var(--cls-neon);' onclick='startVoiceRecognition()'>🎙️ Включить микрофон</button>";
  html += "    </div>";
  html += "  </div>";
  html += "</div>";

  // РЕЖИМ 3: HC-SR04
  html += "<div id='view-2' class='mode-view'>";
  html += "  <div class='panel-card'>";
  html += "    <div class='info-terminal'>";
  html += "      <b>АВТОНОМНЫЙ ХВАТ ДАТЧИКОМ:</b><br/>";
  html += "      Автоматический захват при пересечении зоны ультразвука.";
  html += "    </div>";
  html += "    <div class='panel-card' style='margin-top:15px; background:rgba(0,0,0,0.2);'>";
  html += "      <label>Порог срабатывания: <span class='val-badge'>12 см</span></label>";
  html += "      <label>Задержка удержания: <span class='val-badge'>3 сек</span></label>";
  html += "      <div class='live-metrics' id='sonar-metrics'>Расстояние: -- см</div>";
  html += "      <div id='sonar-status' style='text-align:center; margin-top:8px; font-size:0.8rem; color:var(--cls-muted);'>Поиск объекта...</div>";
  html += "    </div>";
  html += "  </div>";
  html += "</div>";

  // РЕЖИМ 4: ЭМГ
  html += "<div id='view-3' class='mode-view'>";
  html += "  <div class='panel-card'>";
  html += "    <div class='section-title'>Телеметрия мышечной активности ЭМГ</div>";
  html += "    <div class='control-group' style='margin-top:15px;'><label>Большой палец: <span id='emg-0' class='val-badge'>0%</span></label><input type='range' class='emg-s' id='emg-s-0' disabled value='0'/></div>";
  html += "    <div class='control-group'><label>Указательный: <span id='emg-1' class='val-badge'>0%</span></label><input type='range' class='emg-s' id='emg-s-1' disabled value='0'/></div>";
  html += "    <div class='control-group'><label>Средний палец: <span id='emg-2' class='val-badge'>0%</span></label><input type='range' class='emg-s' id='emg-s-2' disabled value='0'/></div>";
  html += "    <div class='control-group'><label>Безымянный: <span id='emg-3' class='val-badge'>0%</span></label><input type='range' class='emg-s' id='emg-s-3' disabled value='0'/></div>";
  html += "    <div class='control-group'><label>Мизинец: <span id='emg-4' class='val-badge'>0%</span></label><input type='range' class='emg-s' id='emg-s-4' disabled value='0'/></div>";
  html += "  </div>";
  html += "</div>";

  html += "</div></div>"; // Конец main-workspace и container
  html += "<script>";
  html += "const SECRET_PIN = '47781842'; let currentMode = 0; let program = []; let isBusy = false; let telemetryTimer = null;";
  html += "const fingers = ['БОЛЬШОЙ ПАЛЕЦ', 'УКАЗАТЕЛЬНЫЙ', 'СРЕДНИЙ ПАЛЕЦ', 'БЕЗЫМЯННЫЙ', 'МИЗИНЕЦ', 'ВСЯ КИСТЬ'];";
  html += "const fingers_eng = ['thumb', 'index', 'middle', 'ring', 'pinky', 'fist'];";
  
  html += "function playSound(type) {";
  html += "  try {";
  html += "    const AudioContext = window.AudioContext || window.webkitAudioContext; if(!AudioContext) return;";
  html += "    const ctx = playSound._ctx || (playSound._ctx = new AudioContext()); const now = ctx.currentTime;";
  html += "    const osc = ctx.createOscillator(); const gain = ctx.createGain(); osc.connect(gain); gain.connect(ctx.destination);";
  html += "    osc.type = 'sine';";
  html += "    if(type==='click'){ osc.frequency.setValueAtTime(600, now); gain.gain.setValueAtTime(0.05, now); gain.gain.exponentialRampToValueAtTime(0.001, now+0.04); osc.start(now); osc.stop(now+0.04); }";
  html += "    else if(type==='add'){ osc.frequency.setValueAtTime(900, now); gain.gain.setValueAtTime(0.05, now); gain.gain.exponentialRampToValueAtTime(0.001, now+0.05); osc.start(now); osc.stop(now+0.05); }";
  html += "    else if(type==='run'){ osc.type='square'; osc.frequency.setValueAtTime(200, now); osc.frequency.linearRampToValueAtTime(500, now+0.2); gain.gain.setValueAtTime(0.04, now); gain.gain.exponentialRampToValueAtTime(0.001, now+0.2); osc.start(now); osc.stop(now+0.2); }";
  html += "    else if(type==='error'){ osc.type='sawtooth'; osc.frequency.setValueAtTime(150, now); gain.gain.setValueAtTime(0.08, now); gain.gain.exponentialRampToValueAtTime(0.001, now+0.15); osc.start(now); osc.stop(now+0.15); }";
  html += "  } catch(e){}";
  html += "}";

  html += "function updateF() { document.getElementById('f-val').innerText = fingers[document.getElementById('f-slider').value]; playSound('click'); }";
  html += "function updateP() { document.getElementById('p-val').innerText = document.getElementById('p-slider').value + '°'; playSound('click'); }";
  html += "function updateW() { document.getElementById('w-val').innerText = document.getElementById('w-slider').value + ' СЕК'; playSound('click'); }";

  html += "function unlockSystemMenu() {";
  html += "  playSound('click'); let code = prompt('ВВЕДИТЕ КЛЮЧ АДМИНИСТРАТОРА ДЛЯ СМЕНЫ РЕЖИМА:');";
  html += "  if(code === SECRET_PIN) {";
  html += "    document.getElementById('lockBtn').style.display = 'none';";
  html += "    const sel = document.getElementById('modeSelect'); sel.style.display = 'block'; sel.value = currentMode;";
  html += "  } else { playSound('error'); alert('ОТКАЗАНО В ДОСТУПЕ: КЛЮЧ НЕВАЛИДЕН'); }";
  html += "}";

  html += "function handleMenuSelect(val) {";
  html += "  playSound('click'); let m = parseInt(val); activateView(m);";
  html += "  document.getElementById('modeSelect').style.display = 'none';";
  html += "  const lBtn = document.getElementById('lockBtn'); lBtn.style.display = 'block';";
  html += "  lBtn.innerText = m === 0 ? '[ Сменить режим работы ] 🔒' : `[ АКТИВЕН РЕЖИМ 0${m+1} ] 🔐`;";
  html += "}";

  html += "function activateView(modeNum) {";
  html += "  currentMode = modeNum; updateStatusBar(modeNum);";
  html += "  document.querySelectorAll('.mode-view').forEach((v, idx) => { v.classList.toggle('active', idx === modeNum); });";
  html += "  fetch('/set_mode?m=' + modeNum);";
  html += "  if(telemetryTimer) { clearInterval(telemetryTimer); telemetryTimer = null; }";
  html += "  if(modeNum === 2 || modeNum === 3) { telemetryTimer = setInterval(pollSensors, 250); }";
  html += "}";

  html += "function updateStatusBar(m) {";
  html += "  const bar = document.getElementById('status-bar');";
  html += "  const labels = [ 'СТАТУС: РУЧНОЙ КОНСТРУКТОР АЛГОРИТМОВ', 'СТАТУС: ГОЛОСОВОЙ МОДУЛЬ RECOGNITION ACTIVE', 'СТАТУС: АВТОНОМНЫЙ ХВАТ (HC-SR04 МОНИТОР)', 'СТАТУС: БИО-ЭМГ ИНТЕРФЕЙС ТЕЛЕМЕТРИИ' ];";
  html += "  bar.innerText = '> ' + (labels[m] || labels);";
  html += "}";

  html += "function addFingerAction() {";
  html += "  playSound('add'); let f_idx = parseInt(document.getElementById('f-slider').value); let p_val = parseInt(document.getElementById('p-slider').value);";
  html += "  document.getElementById('placeholder').style.display = 'none'; let id = Date.now();";
  html += "  program.push({id, type: 'motor', finger: f_idx, angle: p_val});";
  html += "  renderBlock(fingers[f_idx] + ' → ' + p_val + '°', 'algo-block', id);";
  html += "}";

  html += "function addWaitAction() {";
  html += "  playSound('add'); let sec = parseInt(document.getElementById('w-slider').value);";
  html += "  document.getElementById('placeholder').style.display = 'none'; let id = Date.now();";
  html += "  program.push({id, type: 'wait', val: sec});";
  html += "  renderBlock('ПАУЗА ' + sec + ' СЕК', 'algo-block block-wait', id);";
  html += "}";

  html += "function renderBlock(text, className, id) {";
  html += "  let div = document.createElement('div'); div.className = className; div.innerText = text;";
  html += "  div.onclick = () => { playSound('error'); program = program.filter(x => x.id !== id); div.remove(); if(!program.length) document.getElementById('placeholder').style.display = 'block'; };";
  html += "  document.getElementById('queue').appendChild(div);";
  html += "}";

  html += "function clearQueue() { playSound('click'); program = []; document.getElementById('queue').innerHTML = '<div id=\"placeholder\" style=\"color:#445577; width:100%; margin-top:25px; font-size:0.75rem; text-align:center;\">[ СИСТЕМА ГОТОВА К ВВОДУ КОМАНД ]</div>'; }";

  html += "async function runAlgorithm() {";
  html += "  if(isBusy || !program.length) return; isBusy = true; playSound('run');";
  html += "  const btn = document.getElementById('run-btn'); btn.innerText = 'ВЫПОЛНЕНИЕ ЦИКЛА...'; btn.disabled = true;";
  html += "  for(let cmd of program) {";
  html += "    if(cmd.type === 'motor') {";
  html += "      let res = await fetch(`/execute?f=${cmd.finger}&a=${cmd.angle}`);";
  html += "      let statusTxt = await res.text();";
  html += "      if(statusTxt === 'BUSY') { playSound('error'); alert('Роборука занята другим пользователем! Команда проигнорирована.'); break; }";
  html += "      await new Promise(r => setTimeout(r, 600));";
  html += "    } else if(cmd.type === 'wait') { await new Promise(r => setTimeout(r, cmd.val * 1000)); }";
  html += "  }";
  html += "  isBusy = false; btn.innerText = 'ВЫПОЛНИТЬ СКРИПТ'; btn.disabled = false; clearQueue();";
  html += "}";

  html += "function startVoiceRecognition() {";
  html += "  window.SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;";
  html += "  if(!window.SpeechRecognition) { alert('Ваш браузер не поддерживает голосовое управление.'); return; }";
  html += "  const rec = new SpeechRecognition(); rec.lang = 'ru-RU'; rec.start();";
  html += "  const btn = document.getElementById('voiceBtn'); btn.innerText = '⏳ Слушаю команду...'; playSound('click');";
  html += "  rec.onresult = async function(e) {";
  html += "    let cmd = e.results.transcript.toLowerCase(); btn.innerText = '🎙️ Распознано: ' + cmd.toUpperCase();";
  html += "    if(cmd.includes('кулак') || cmd.includes('сожми')) { playSound('run'); let res = await fetch('/execute?f=5&a=180'); let txt = await res.text(); if(txt==='BUSY') alert('Устройство занято!'); }";
  html += "    else if(cmd.includes('ладонь') || cmd.includes('открой')) { playSound('run'); let res = await fetch('/execute?f=5&a=0'); let txt = await res.text(); if(txt==='BUSY') alert('Устройство занято!'); }";
  html += "  };";
  html += "  rec.onerror = () => { btn.innerText = '🎙️ Ошибка записи'; playSound('error'); };";
  html += "}";

  html += "async function pollSensors() {";
  html += "  try {";
  html += "    let res = await fetch('/sensor_state'); if(!res.ok) return; let data = await res.json();";
  html += "    if(currentMode === 2) {"; 
  html += "      document.getElementById('sonar-metrics').innerText = 'Расстояние: ' + data.dist_cm + ' см';";
  html += "      let statusDiv = document.getElementById('sonar-status');";
  html += "      if(data.dist_cm <= 12 && data.dist_cm > 0) { statusDiv.innerText = 'Объект захвачен! Отпускание через ' + data.timer_s + ' сек'; statusDiv.style.color = 'var(--cls-neon)'; }";
  html += "      else { statusDiv.innerText = 'Поиск объекта (зона срабатывания <= 12 см)...'; statusDiv.style.color = 'var(--cls-muted)'; }";
  html += "    }";
  html += "    if(currentMode === 3) {"; 
  html += "      for(let i=0; i<5; i++) { document.getElementById('emg-'+i).innerText = data.emg_pct + '%'; document.getElementById('emg-s-'+i).value = data.emg_pct; }";
  html += "    }";
  html += "  } catch(e) {}";
  html += "}";

  html += "function startGlobalSync() {";
  html += "  setInterval(async () => {";
  html += "    try {";
  html += "      let res = await fetch('/get_mode'); if(!res.ok) return; let m = parseInt(await res.text());";
  html += "      if(m !== currentMode) { currentMode = m; updateStatusBar(m); document.querySelectorAll('.mode-view').forEach((v, idx) => { v.classList.toggle('active', idx === m); }); }";
  html += "    } catch(e) {}";
  html += "  }, 800);";
  html += "}";
  html += "window.addEventListener('load', () => { startGlobalSync(); activateView(0); });";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}
// Обработчик изменения режима работы
void handleSetMode() {
  if (server.hasArg("m")) {
    currentMode = server.arg("m").toInt();
    Serial.printf("[SYSTEM] Режим изменен пользователем на: %d\n", currentMode);
    sendModeToUno(currentMode);
  }
  server.send(200, "text/plain", "OK");
}

// Эндпоинт для автосинхронизации вкладок пользователей в реальном времени
void handleGetMode() {
  server.send(200, "text/plain", String(currentMode));
}

// Главный обработчик выполнения физических команд (С ЗАЩИТОЙ ОТ ОДНОВРЕМЕННЫХ ПОЛЬЗОВАТЕЛЕЙ)
void handleExecute() {
  if (isRobotBusy) {
    if (millis() < robotBusyUntil) {
      server.send(200, "text/plain", "BUSY"); 
      Serial.println("[SECURITY] Роборука занята. Запрос нового пользователя отклонен.");
      return;
    } else {
      isRobotBusy = false; 
    }
  }

  int f = server.hasArg("f") ? server.arg("f").toInt() : -1;
  int a = server.hasArg("a") ? server.arg("a").toInt() : 0;

  if (f < 0 || f > 5 || a < 0 || a > 180) {
    server.send(200, "text/plain", "ERROR: Invalid parameters");
    return;
  }

  int tempFingers[5];
  for (int i = 0; i < 5; i++) tempFingers[i] = currentFingers[i];

  if (f == 5) { 
    for (int i = 0; i < 5; i++) tempFingers[i] = a;
  } else {      
    tempFingers[f] = a;
  }

  if (currentMode == 0 || currentMode == 1) {
    if (!isValidFingerConfig(tempFingers) || !isValidConstructorMove(f, a, currentFingers)) {
      server.send(200, "text/plain", "ERROR: Safety Blocked");
      Serial.println("[ANTI-JAM] Попытка совершить анатомически опасный жест заблокирована!");
      return;
    }
  }

  isRobotBusy = true;
  robotBusyUntil = millis() + 600;

  if (f == 5) {
    for (int i = 0; i < 5; i++) currentFingers[i] = a;
    sendCommandToUno(0, a); 
  } else {
    currentFingers[f] = a;
    sendCommandToUno(f + 1, a); 
  }

  server.send(200, "text/plain", "OK");
}

// Запрос сырых данных у Arduino Uno по интерфейсу I2C
uint16_t requestSensorFromUno(byte sensorType) {
  Wire.beginTransmission(ARDUINO_UNO_I2C_ADDR);
  Wire.write((byte)254);   
  Wire.write(sensorType); 
  Wire.endTransmission();

  Wire.requestFrom(ARDUINO_UNO_I2C_ADDR, 2); 
  if (Wire.available() >= 2) {
    byte high = Wire.read();
    byte low = Wire.read();
    return (high << 8) | low;
  }
  return 0;
}

// Передача JSON-пакета телеметрии на фронтенд для графиков и мониторов
void handleSensorState() {
  uint16_t distance = requestSensorFromUno(1);
  uint16_t emgRaw = requestSensorFromUno(2);
  uint16_t distanceTimer = requestSensorFromUno(3); 

  int emgPct = (emgRaw * 100) / 1023;
  if (emgPct > 100) emgPct = 100;
  if (emgPct < 0) emgPct = 0;

  String json = "{";
  json += "\"dist_cm\":" + String(distance) + ",";
  json += "\"emg_pct\":" + String(emgPct) + ",";
  json += "\"timer_s\":" + String(distanceTimer) + ",";
  json += "\"mode\":" + String(currentMode);
  json += "}";

  server.send(200, "application/json", json);
}

// Перенаправление на корень при ошибках 404
void handleNotFound() {
  server.sendHeader("Location", "http://10.10.10.1", true);
  server.send(302, "text/plain", "");
}

// НАСТРОЙКА ЖЕЛЕЗА И СЕРВЕРА СЕТИ ДЛЯ ESP32
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n[ESP32] Запуск управляющего ядра...");

  // Инициализация I2C Master
  Wire.begin(21, 22);
  Serial.println("[I2C] Аппаратная шина запущена на GPIO 21 (SDA) и GPIO 22 (SCL)");

  // Конфигурация автономной Wi-Fi точки доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMask);
  WiFi.softAP(ssid, password);
  
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.printf("[WIFI] Точка доступа '%s' активна. IP: 10.10.10.1\n", ssid);

  // Маршрутизация веб-запросов
  server.on("/", handleRoot);
  server.on("/set_mode", handleSetMode);
  server.on("/get_mode", handleGetMode);
  server.on("/execute", handleExecute);
  server.on("/sensor_state", handleSensorState);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[SYSTEM] Веб-сервер успешно запущен. Ожидание клиентов...");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(2); 
}
