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

int currentFingers[5] = {0, 0, 0, 0, 0};
int currentMode = 0;
bool isRobotBusy = false;
unsigned long macroStartTime = 0;
unsigned long macroDuration = 0;

void sendCommandToUno(byte fingerNum, byte angle) {
  Wire.beginTransmission(ARDUINO_UNO_I2C_ADDR);
  Wire.write(fingerNum);
  Wire.write(angle);
  Wire.endTransmission();
  Serial.print("[I2C] Sent: Finger ");
  Serial.print(fingerNum);
  Serial.print(" Angle ");
  Serial.println(angle);
}

bool isValidFingerConfig(int fingers[5]) {
  int thumb = fingers[0];
  int index = fingers[1];
  int middle = fingers[2];
  int ring = fingers[3];
  int pinky = fingers[4];
  if (middle > 90 && thumb < 90 && index < 90 && ring < 90 && pinky < 90) {
    return false;
  }
  if (middle > 90 && thumb > 90 && index < 90 && ring < 90 && pinky < 90) {
    return false;
  }
  return true;
}

bool isValidConstructorMove(int fingerIdx, int targetAngle, int currentState[5]) {
  int thumb = currentState[0];
  int index = currentState[1];
  int middle = currentState[2];
  int ring = currentState[3];
  int pinky = currentState[4];
  int tempFingers[5];
  for(int i = 0; i < 5; i++) tempFingers[i] = currentState[i];
  tempFingers[fingerIdx] = targetAngle;
  if (!isValidFingerConfig(tempFingers)) {
    return false;
  }
  bool allClenched = (thumb > 90 && index > 90 && middle > 90 && ring > 90 && pinky > 90);
  if (allClenched && targetAngle <= 90) {
    int openCount = 0;
    int openFingers[5] = {0};
    for(int i = 0; i < 5; i++) {
      if (tempFingers[i] <= 90) {
        openFingers[openCount++] = i;
      }
    }
    if (openCount == 1 && openFingers[0] == 2) {
      return false;
    }
    if (openCount == 2 && ((openFingers[0] == 0 && openFingers[1] == 2) || (openFingers[0] == 2 && openFingers[1] == 0))) {
      return false;
    }
  }
  bool allOpen = (thumb < 90 && index < 90 && middle < 90 && ring < 90 && pinky < 90);
  if (allOpen && targetAngle > 90) {
    int clenchedCount = 0;
    int clenchedFingers[5] = {0};
    for(int i = 0; i < 5; i++) {
      if (tempFingers[i] > 90) {
        clenchedFingers[clenchedCount++] = i;
      }
    }
    if (clenchedCount == 4 && tempFingers[2] < 90) {
      if ((tempFingers[0] > 90 && tempFingers[1] > 90 && tempFingers[3] > 90 && tempFingers[4] > 90) && tempFingers[2] < 90) {
        return false;
      }
    }
    if ((fingerIdx == 1 || fingerIdx == 3 || fingerIdx == 4) && targetAngle > 90) {
      if (tempFingers[2] < 90 && tempFingers[0] < 90) {
        return false;
      }
    }
  }
  return true;
}
void handleRoot() {
  String html = "<!DOCTYPE html><html lang='ru'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>InMoov i2 OS - Hotline Cyberpunk</title>";
  html += "<style>";
  html += "@import url('https://googleapis.com');";
  html += ":root { --hl-pink: #ff007f; --hl-cyan: #00ffff; --hl-green: #39ff14; --hl-purple: #bd00ff; --bg-dark: #05010a; --card-bg: rgba(26, 0, 51, 0.85); }";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
  html += "body { background-color: var(--bg-dark); color: #e2e8f0; font-family: 'Courier Prime', monospace, sans-serif; text-align: center; padding: 40px 20px; min-height: 100vh; position: relative; overflow-x: hidden; }";
  html += ".atmosphere { position: fixed; inset: 0; z-index: -2; pointer-events: none; overflow: hidden; }";
  html += ".sky-sunset { position: absolute; inset: 0; background: linear-gradient(180deg, #05010a 0%, #1a0033 35%, #3d0066 55%, #ff007f 78%, #ffaa00 92%); opacity: 0.85; }";
  html += ".bg-sun { position: absolute; left: 50%; top: 75%; z-index: -3; width: min(420px, 85vw); height: min(420px, 85vw); transform: translate(-50%, -50%); background: linear-gradient(180deg, #ffcc44 0%, #ff6600 35%, #ff007f 70%, #660044 100%); border-radius: 50%; box-shadow: 0 0 50px #ffaa00, 0 0 80px #ff007f; filter: brightness(1.2) saturate(1.15); -webkit-mask-image: repeating-linear-gradient(180deg, #000 0px, #000 14px, transparent 14px, transparent 20px); mask-image: repeating-linear-gradient(180deg, #000 0px, #000 14px, transparent 14px, transparent 20px); }";
  html += ".stars { position: absolute; inset: 0; background-image: radial-gradient(1px 1px at 8% 12%, rgba(255, 255, 255, 0.8), transparent), radial-gradient(1px 1px at 22% 28%, rgba(255, 255, 255, 0.6), transparent), radial-gradient(1.5px 1.5px at 35% 8%, rgba(0, 240, 255, 0.7), transparent), radial-gradient(1px 1px at 61% 15%, rgba(255, 255, 255, 0.8), transparent), radial-gradient(1.5px 1.5px at 88% 10%, rgba(255, 0, 127, 0.6), transparent); }";
  html += "@keyframes title-glow { 0%, 100% { text-shadow: 0 0 10px var(--hl-pink), 0 0 20px var(--hl-pink); } 50% { text-shadow: 0 0 16px var(--hl-pink), 0 0 32px var(--hl-pink), 0 0 48px rgba(0, 240, 255, 0.25); } }";
  html += "h1 { font-size: 1.6rem; letter-spacing: 4px; color: var(--hl-pink); margin-bottom: 30px; text-align: center; animation: title-glow 3s ease-in-out infinite; text-transform: uppercase; }";
  html += "h3 { color: var(--hl-cyan); font-size: 0.9rem; text-transform: uppercase; letter-spacing: 2px; margin-bottom: 15px; text-align: left; }";
  html += ".container { width: 100%; max-width: 450px; position: relative; margin: 0 auto; z-index: 3; }";
  html += ".retro-card { background: var(--card-bg); border: 2px solid var(--hl-cyan); box-shadow: 0 0 15px rgba(0, 240, 255, 0.4); border-radius: 6px; backdrop-filter: blur(6px); padding: 20px; margin-bottom: 20px; text-align: left; }";
  html += "#status-bar { background: #000; border: 2px solid var(--hl-green); padding: 12px; font-weight: bold; color: var(--hl-green); margin-bottom: 20px; text-transform: uppercase; letter-spacing: 1px; box-shadow: 0 0 10px rgba(57, 255, 20, 0.3); text-align: center; font-size: 0.9rem; }";
  html += ".btn-hl { display: block; width: 100%; text-align: center; background: transparent; color: var(--hl-pink); border: 2px solid var(--hl-pink); padding: 14px; font-weight: bold; letter-spacing: 3px; text-transform: uppercase; cursor: pointer; transition: all 0.2s ease; box-shadow: 0 0 10px rgba(255, 0, 127, 0.2); font-family: 'Courier Prime', monospace; }";
  html += ".btn-hl:hover, .btn-hl:active { background: var(--hl-pink); color: #000; box-shadow: 0 0 20px var(--hl-pink); }";
  html += ".select-hl { background: #000; border: 2px solid var(--hl-pink); color: var(--hl-pink); padding: 12px; width: 100%; font-family: 'Courier Prime', monospace; font-weight: bold; font-size: 0.95rem; outline: none; text-transform: uppercase; box-sizing: border-box; border-radius: 4px; box-shadow: 0 0 10px rgba(255, 0, 127, 0.3); }";
  html += ".constructor-zone { background: rgba(0, 0, 0, 0.75); border: 2px solid var(--hl-purple); min-height: 110px; margin-bottom: 20px; padding: 15px; display: flex; flex-wrap: wrap; gap: 8px; align-content: flex-start; border-radius: 6px; }";
  html += ".algo-block { background: rgba(0, 212, 255, 0.08); color: var(--hl-cyan); padding: 8px 12px; border: 1px solid var(--hl-cyan); border-radius: 6px; font-size: 11px; font-weight: bold; cursor: pointer; transition: 0.2s; text-transform: uppercase; }";
  html += ".algo-block:hover { background: var(--hl-pink); color: #fff; border-color: var(--hl-pink); }";
  html += ".block-wait { border-color: #ffcc00; color: #ffcc00; background: rgba(255, 204, 0, 0.05); }";
  html += ".controls { margin-bottom: 20px; text-align: left; }";
  html += "label { font-size: 0.8em; color: #888; text-transform: uppercase; letter-spacing: 1.5px; font-weight: bold; display: block; margin-bottom: 5px; }";
  html += ".val-display { float: right; color: var(--hl-cyan); font-size: 0.95em; text-shadow: 0 0 5px var(--hl-cyan); }";
  html += "input[type=range] { -webkit-appearance: none; width: 100%; background: transparent; margin: 15px 0; }";
  html += "input[type=range]:focus { outline: none; }";
  html += "input[type=range]::-webkit-slider-runnable-track { width: 100%; height: 6px; cursor: pointer; background: #1a1f29; border-radius: 3px; border: 1px solid #2a3344; }";
  html += "input[type=range]::-webkit-slider-thumb { height: 22px; width: 14px; border-radius: 2px; background: var(--hl-green); cursor: pointer; -webkit-appearance: none; margin-top: -8px; box-shadow: 0 0 10px var(--hl-green); border: 1px solid #fff; }";
  html += ".btn-add { width: 100%; background: transparent; color: var(--hl-cyan); border: 1px solid var(--hl-cyan); padding: 14px; border-radius: 12px; font-weight: bold; cursor: pointer; margin-top: 15px; text-transform: uppercase; font-size: 11px; transition: 0.3s; font-family: 'Courier Prime', monospace; }";
  html += ".btn-add:hover { background: var(--hl-cyan); color: #000; box-shadow: 0 0 15px rgba(0, 212, 255, 0.3); }";
  html += ".btn-run { width: 100%; background: var(--hl-pink); color: #fff; padding: 20px; border: none; border-radius: 16px; font-size: 1.1em; font-weight: bold; cursor: pointer; margin-top: 15px; text-transform: uppercase; letter-spacing: 3px; box-shadow: 0 10px 30px rgba(255,0,85,0.3); font-family: 'Courier Prime', monospace; }";
  html += ".btn-clear { background: none; border: none; color: #484f58; cursor: pointer; margin-top: 15px; font-size: 0.8em; width: 100%; font-family: 'Courier Prime', monospace; text-transform: uppercase; }";
  html += ".disabled-ui { opacity: 0.05; pointer-events: none; filter: grayscale(100%) blur(4px); transition: 0.4s; }";
  html += "</style></head><body>";
  html += "<div class='atmosphere'><div class='sky-sunset'></div><div class='stars'></div><div class='bg-sun'></div></div>";
  html += "<div class='container'><h1>InMoov i2 System</h1><div id='status-bar'>> СТАТУС: РУЧНОЙ КОНСТРУКТОР СЕТИ</div>";
  html += "<h3>ВЫБОР РЕЖИМА УПРАВЛЕНИЯ</h3>";
  html += "<div class='retro-card' style='border-color: var(--hl-pink); box-shadow: 0 0 15px rgba(255, 0, 127, 0.3);'>";
  html += "<button id='lockBtn' class='btn-hl' onclick='unlockSystemMenu()'>[ СМЕНИТЬ РЕЖИМ СИСТЕМЫ ] 🔒</button>";
  html += "<select id='modeSelect' class='select-hl' style='display: none;' onchange='handleMenuSelect(this.value)'>";
  html += "<option value='0'>[ 01 // РУЧНОЙ КОНСТРУКТОР ]</option>";
  html += "<option value='1'>[ 02 // БИО-ЭМГ ИНТЕРФЕЙС ]</option>";
  html += "<option value='2'>[ 03 // АВТОНОМНЫЙ ХВАТ ]</option>";
  html += "<option value='3'>[ 04 // ГОЛОСОВОЙ МОДУЛЬ ]</option>";
  html += "</select></div>";
  html += "<div id='secure-interactive-area'><h3>ОЧЕРЕДЬ АЛГОРИТМА КОМАНД</h3>";
  html += "<div class='constructor-zone' id='queue'><div id='placeholder' style='color: #444; width: 100%; margin-top: 35px; font-size: 0.7em; letter-spacing: 2px; text-align: center;'>ОЖИДАНИЕ КОМАНД...</div></div>";
  html += "<div class='retro-card'><div class='controls'><label>ВЫБОР ПРИВОДА: <span id='f-val' class='val-display'>БОЛЬШОЙ ПАЛЕЦ</span></label>";
  html += "<input type='range' id='f-slider' min='0' max='5' step='1' value='0' oninput='updateF()'></div>";
  html += "<div class='controls'><label>УГОЛ СЖАТИЯ: <span id='p-val' class='val-display'>0°</span></label>";
  html += "<input type='range' id='p-slider' min='0' max='180' step='5' value='0' oninput='updateP()'>";
  html += "<button class='btn-add' onclick='addFingerAction()'>ДОБАВИТЬ В ОЧЕРЕДЬ</button></div></div>";
  html += "<div class='retro-card'><div class='controls'><label>ЗАДЕРЖКА: <span id='w-val' class='val-display'>1 СЕК</span></label>";
  html += "<input type='range' id='w-slider' min='1' max='10' step='1' value='1' oninput='updateW()'>";
  html += "<button class='btn-add' style='border-color: #ffcc00; color: #ffcc00;' onclick='addWaitAction()'>ДОБАВИТЬ ПАУЗУ</button></div></div>";
  html += "<button class='btn-run' onclick='runAlgorithm()' id='run-btn'>ИСПОЛНИТЬ ЦИКЛ</button>";
  html += "<button class='btn-clear' onclick='clearQueue()'>ПОЛНЫЙ СБРОС</button></div>";
  html += "<div id='voice-module-zone' style='display: none; margin-top: 25px;'><h3>ГОЛОСОВОЙ ИНТЕРФЕЙС OPERATOR</h3>";
  html += "<div class='retro-card' style='border-color: var(--hl-green); box-shadow: 0 0 15px rgba(57, 255, 20, 0.2);'>";
  html += "<button id='voiceBtn' class='btn-hl' style='border-color: var(--hl-green); color: var(--hl-green);' onclick='startVoiceRecognition()'>🎙️ НАЧАТЬ СКАНИРОВАНИЕ ГОЛОСА</button></div></div></div>";
  html += "<script>";
  html += "const SECRET_PIN = '47781842'; let currentMode = 0; let program = []; let isBusy = false;";
  html += "const fingers = ['БОЛЬШОЙ ПАЛЕЦ', 'УКАЗАТЕЛЬНЫЙ', 'СРЕДНИЙ ПАЛЕЦ', 'БЕЗЫМЯННЫЙ', 'МИЗИНЕЦ', 'ВСЯ КИСТЬ'];";
  html += "const fingers_eng = ['thumb', 'index', 'middle', 'ring', 'pinky', 'fist'];";
  html += "const audioCtx = new (window.AudioContext || window.webkitAudioContext)();";
  html += "function playSound(type) { if (audioCtx.state === 'suspended') audioCtx.resume(); const osc = audioCtx.createOscillator(); const gain = audioCtx.createGain(); osc.connect(gain); gain.connect(audioCtx.destination); if (type === 'click') { osc.type = 'sine'; osc.frequency.setValueAtTime(800, audioCtx.currentTime); gain.gain.setValueAtTime(0.1, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.05); osc.start(); osc.stop(audioCtx.currentTime + 0.05); } else if (type === 'run') { osc.type = 'sawtooth'; osc.frequency.setValueAtTime(150, audioCtx.currentTime); osc.frequency.linearRampToValueAtTime(400, audioCtx.currentTime + 0.3); gain.gain.setValueAtTime(0.1, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.3); osc.start(); osc.stop(audioCtx.currentTime + 0.3); } }";
  html += "function updateF() { document.getElementById('f-val').innerText = fingers[document.getElementById('f-slider').value]; playSound('click'); }";
  html += "function updateP() { document.getElementById('p-val').innerText = document.getElementById('p-slider').value + '°'; playSound('click'); }";
  html += "function updateW() { document.getElementById('w-val').innerText = document.getElementById('w-slider').value + ' СЕК'; playSound('click'); }";
  html += "function unlockSystemMenu() { playSound('click'); let userInput = prompt('ENTER ACCESS CODE // ВВЕДИТЕ ПИН-КОД АДМИНИСТРАТОРА:'); if (userInput === SECRET_PIN) { alert('ACCESS GRANTED // МЕНЮ РАЗБЛОКИРОВАНО'); document.getElementById('lockBtn').style.display = 'none'; const selectMenu = document.getElementById('modeSelect'); selectMenu.style.display = 'block'; selectMenu.value = currentMode; } else { alert('ACCESS DENIED // ОТКАЗАНО В ДОСТУПЕ'); } }";
  html += "function handleMenuSelect(selectedMode) { playSound('click'); activateMode(parseInt(selectedMode)); document.getElementById('modeSelect').style.display = 'none'; const lockBtn = document.getElementById('lockBtn'); lockBtn.style.display = 'block'; if(parseInt(selectedMode) === 0) { lockBtn.style.borderColor = 'var(--hl-pink)'; lockBtn.style.color = 'var(--hl-pink)'; lockBtn.innerText = '[ СМЕНИТЬ РЕЖИМ СИСТЕМЫ ] 🔒'; } else { lockBtn.style.borderColor = 'var(--hl-green)'; lockBtn.style.color = 'var(--hl-green)'; lockBtn.innerText = `[ АКТИВЕН РЕЖИМ 0${parseInt(selectedMode) + 1} ] 🔐`; } }";
  html += "function activateMode(modeNum) { currentMode = modeNum; const statusBar = document.getElementById('status-bar'); const area = document.getElementById('secure-interactive-area'); const voiceZone = document.getElementById('voice-module-zone'); fetch('/set_mode?m=' + modeNum); if (modeNum === 0) { statusBar.innerHTML = '> СТАТУС: РУЧНОЙ КОНСТРУКТОР СЕТИ'; statusBar.style.borderColor = 'var(--hl-green)'; statusBar.style.color = 'var(--hl-green)'; area.classList.remove('disabled-ui'); voiceZone.style.display = 'none'; } else { area.classList.add('disabled-ui'); statusBar.style.borderColor = 'var(--hl-pink)'; statusBar.style.color = 'var(--hl-pink)'; if (modeNum === 1) statusBar.innerHTML = '> ЗАБЛОКИРОВАНО // БИО-ЭМГ ИНТЕРФЕЙС'; if (modeNum === 2) statusBar.innerHTML = '> ЗАБЛОКИРОВАНО // АВТОНОМНЫЙ ХВАТ'; if (modeNum === 3) { statusBar.innerHTML = '> ЗАБЛОКИРОВАНО // ГОЛОСОВОЙ МОДУЛЬ'; voiceZone.style.display = 'block'; } else { voiceZone.style.display = 'none'; } } }";
  html += "function addFingerAction() { playSound('click'); let f_idx = document.getElementById('f-slider').value; let p_val = document.getElementById('p-slider').value; document.getElementById('placeholder').style.display = 'none'; let id = Date.now(); program.push({id, type: 'motor', finger: f_idx, angle: p_val}); updateUI(fingers[f_idx] + ' : ' + p_val + '°', 'algo-block', id); }";
  html += "function addWaitAction() { playSound('click'); let sec = document.getElementById('w-slider').value; document.getElementById('placeholder').style.display = 'none'; let id = Date.now(); program.push({id, type: 'wait', val: sec}); updateUI('ПАУЗА ' + sec + ' СЕК', 'algo-block block-wait', id); }";
  html += "function updateUI(text, className, id) { let div = document.createElement('div'); div.className = className; div.innerText = text; div.onclick = () => { playSound('click'); program = program.filter(i => i.id !== id); div.remove(); if(!program.length) document.getElementById('placeholder').style.display = 'block'; }; document.getElementById('queue').appendChild(div); }";
  html += "function clearQueue() { playSound('click'); program = []; document.getElementById('queue').innerHTML = '<div id=\"placeholder\" style=\"color: #444; width: 100%; margin-top: 35px; font-size: 0.7em; letter-spacing: 2px; text-align: center;\">ОЖИДАНИЕ КОМАНД...</div>'; }";
  html += "async function runAlgorithm() { if(isBusy || !program.length) return; isBusy = true; playSound('run'); const btn = document.getElementById('run-btn'); btn.innerText = 'ВЫПОЛНЕНИЕ...'; btn.style.background = '#222'; const zone = document.getElementById('queue'); for (let i = 0; i < program.length; i++) { let cmd = program[i]; if (zone.children[i+1]) zone.children[i+1].style.borderColor = 'var(--hl-pink)'; if (cmd.type === 'motor') { let res = await fetch(`/execute?f=${cmd.finger}&a=${cmd.angle}`); await new Promise(r => setTimeout(r, 500)); } else if (cmd.type === 'wait') { await new Promise(r => setTimeout(r, cmd.val * 1000)); } if (zone.children[i+1]) zone.children[i+1].style.borderColor = 'var(--hl-cyan)'; } isBusy = false; btn.innerText = 'ИСПОЛНИТЬ ЦИКЛ'; btn.style.background = 'var(--hl-pink)'; clearQueue(); }";
  html += "function startVoiceRecognition() { window.SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition; if (!window.SpeechRecognition) { alert('Браузер не поддерживает голос'); return; } const recognition = new SpeechRecognition(); recognition.lang = 'ru-RU'; recognition.start(); const btn = document.getElementById('voiceBtn'); btn.innerHTML = '⏳ СЛУШАЮ...'; recognition.onresult = function(event) { const command = event.results[0].transcript.toLowerCase(); btn.innerHTML = '🎙️ ПОЛУЧЕНО: ' + command.toUpperCase(); if (command.includes('кулак') || command.includes('сожми')) { playSound('run'); for(let i=0; i<5; i++) fetch(`/execute?f=${i}&a=180`); } else if (command.includes('ладонь') || command.includes('открой')) { playSound('run'); for(let i=0; i<5; i++) fetch(`/execute?f=${i}&a=0`); } }; recognition.onerror = () => { btn.innerHTML = '🎙️ ОШИБКА'; }; }";
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

  // СИСТЕМНОЕ ИСПРАВЛЕНИЕ: Переход на извлечение аргументов через метод .arg() вместо булевой .hasArg()
  int f = server.hasArg("f") ? server.arg("f").toInt() : -1;
  int a = server.hasArg("a") ? server.arg("a").toInt() : 0;

  if (a < 0 || a > 180 || f < 0 || f > 5) {
    server.send(200, "text/plain", "ERROR: Invalid params");
    return;
  }

  int tempFingers[5];
  for(int i = 0; i < 5; i++) tempFingers[i] = currentFingers[i];
  
  if (f == 5) {
    for(int i = 0; i < 5; i++) tempFingers[i] = a;
  } else {
    tempFingers[f] = a;
  }

  if (currentMode == 0 || currentMode == 3) {
    if (!isValidFingerConfig(tempFingers)) {
      server.send(200, "text/plain", "ERROR: Forbidden position");
      Serial.println("[SAFETY] Forbidden finger position blocked");
      return;
    }
    if (!isValidConstructorMove(f, a, currentFingers)) {
      server.send(200, "text/plain", "ERROR: Invalid move (safety)");
      Serial.println("[SAFETY] Invalid constructor move blocked");
      return;
    }
  }

  isRobotBusy = true;
  macroStartTime = millis();
  macroDuration = 500;

  for(int i = 0; i < 5; i++) currentFingers[i] = tempFingers[i];

  if (f == 5) {
    for(int i = 0; i < 5; i++) {
      sendCommandToUno(i + 1, a);
      delay(30);
    }
  } else {
    sendCommandToUno(f + 1, a);
  }
  server.send(200, "text/plain", "OK");
}

void handleNotFound() {
  server.sendHeader("Location", "http://10.10.10.1", true);
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
