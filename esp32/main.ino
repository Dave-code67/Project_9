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
int currentFingers[5] = {0, 0, 0, 0, 0};

// Защита «Один хозяин»
bool isRobotBusy = false;
unsigned long macroStartTime = 0;
unsigned long macroDuration = 0;

void sendCommandToUno(byte fingerNum, byte state) {
  Wire.beginTransmission(ARDUINO_UNO_I2C_ADDR);
  Wire.write(fingerNum); // 1-5 для пальцев, 0 для всей кисти
  Wire.write(state);     // 0 - разогнуть, 1 - согнуть
  Wire.endTransmission();
}

void handleRoot() {
  String html = "<!DOCTYPE html><html lang='ru'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>InMoov i2 OS - Hotline Cyberpunk</title>";
  html += "<style>";
  html += ":root { --neon: #00ffcc; --bg: #05050a; --panel: #0d1117; --danger: #ff007f; --grid: rgba(255, 0, 127, 0.05); }";
  html += "body { font-family: 'Courier New', monospace; background: var(--bg); background-image: linear-gradient(var(--grid) 1px, transparent 1px), linear-gradient(90deg, var(--grid) 1px, transparent 1px); background-size: 35px 35px; color: var(--neon); margin: 0; display: flex; justify-content: center; align-items: center; min-height: 100vh; }";
  html += ".container { width: 95%; max-width: 450px; padding: 30px; background: rgba(15, 15, 30, 0.9); backdrop-filter: blur(20px); border: 2px solid var(--danger); border-radius: 4px; box-shadow: 0 0 25px rgba(255,0,127,0.4); box-sizing: border-box; }";
  html += "h2 { text-transform: uppercase; letter-spacing: 5px; font-size: 1.1em; color: #fff; margin-bottom: 25px; text-shadow: 0 0 10px var(--danger); text-align: center; }";
  html += ".constructor-zone { background: rgba(0, 0, 0, 0.7); border: 1px solid var(--danger); min-height: 110px; margin-bottom: 25px; padding: 15px; border-radius: 4px; display: flex; flex-wrap: wrap; gap: 8px; align-content: flex-start; box-sizing: border-box; }";
  html += ".algo-block { background: rgba(0, 255, 204, 0.05); color: var(--neon); padding: 8px 12px; border: 1px solid var(--neon); border-radius: 2px; font-size: 10px; font-weight: bold; cursor: pointer; transition: 0.2s; box-shadow: 0 0 5px rgba(0,255,204,0.2); }";
  html += ".algo-block:hover { background: var(--danger); color: #fff; border-color: var(--danger); box-shadow: 0 0 10px var(--danger); }";
  html += ".block-wait { border-color: #ffff00; color: #ffff00; background: rgba(255, 255, 0, 0.03); box-shadow: 0 0 5px rgba(255,255,0,0.2); }";
  html += ".controls { margin-bottom: 20px; text-align: left; }";
  html += "label { font-size: 11px; color: #00ffcc; text-transform: uppercase; letter-spacing: 1.5px; font-weight: bold; display: block; margin-bottom: 5px; text-shadow: 0 0 3px var(--neon); }";
  html += ".val-display { float: right; color: #ffff00; font-size: 1em; text-shadow: 0 0 5px #ffff00; }";
  html += "input[type=range] { -webkit-appearance: none; width: 100%; background: #111; height: 8px; border-radius: 2px; border: 1px solid var(--danger); margin: 10px 0; }";
  html += "input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; height: 20px; width: 20px; border-radius: 50%; background: var(--danger); cursor: pointer; box-shadow: 0 0 10px var(--danger); border: 1px solid #fff; }";
  html += ".btn-add { width: 100%; background: transparent; color: var(--neon); border: 1px solid var(--neon); padding: 12px; font-weight: bold; cursor: pointer; margin-top: 10px; text-transform: uppercase; font-size: 11px; transition: 0.2s; letter-spacing: 1px; }";
  html += ".btn-add:hover { background: var(--neon); color: #000; box-shadow: 0 0 15px var(--neon); }";
  html += ".btn-run { width: 100%; background: var(--danger); color: #fff; padding: 18px; border: none; font-size: 1.1em; font-weight: bold; cursor: pointer; margin-top: 15px; text-transform: uppercase; letter-spacing: 3px; box-shadow: 0 0 20px rgba(255,0,127,0.5); }";
  html += ".btn-run:hover { background: #ff3399; box-shadow: 0 0 30px var(--danger); }";
  html += ".btn-clear { background: none; border: none; color: #555; cursor: pointer; margin-top: 15px; font-size: 0.8em; width: 100%; text-transform: uppercase; letter-spacing: 1px; }";
  html += ".btn-clear:hover { color: var(--danger); }";
  html += "</style></head><body>";

  html += "<div class='container'>";
  html += "<h2>InMoov Cyber System</h2>"; // Название без молний
  html += "<div class='constructor-zone' id='queue'>";
  html += "<div id='placeholder' style='color: #333; width: 100%; margin-top: 35px; font-size: 10px; letter-spacing: 2px; text-align: center;'>ОЖИДАНИЕ КОМАНД...</div>";
  html += "</div>";

  // Выбор привода
  html += "<div class='controls'>";
  html += "<label>ВЫБОР ПРИВОДА: <span id='f-val' class='val-display'>БОЛЬШОЙ ПАЛЕЦ</span></label>";
  html += "<input type='range' id='f-slider' min='0' max='5' step='1' value='0' oninput='updateF()'>";
  html += "</div>";

  // Интенсивность (Кнопка «ДОБАВИТЬ В ОЧЕРЕДЬ» теперь ТУТ)
  html += "<div class='controls'>";
  html += "<label>ИНТЕНСИВНОСТЬ: <span id='p-val' class='val-display'>Разогнуть</span></label>";
  html += "<input type='range' id='p-slider' min='0' max='2' step='1' value='0' oninput='updateP()'>";
  html += "<button class='btn-add' onclick='addFingerAction()'>ДОБАВИТЬ В ОЧЕРЕДЬ</button>";
  html += "</div>";

  // Задержка
  html += "<div class='controls' style='margin-top: 20px;'>";
  html += "<label>ЗАДЕРЖКА: <span id='w-val' class='val-display'>1 СЕК</span></label>";
  html += "<input type='range' id='w-slider' min='1' max='20' step='1' value='1' oninput='updateW()'>";
  html += "<button class='btn-add' style='border-color: #ffff00; color: #ffff00;' onclick='addWaitAction()'>ДОБАВИТЬ ПАУЗУ</button>";
  html += "</div>";

  html += "<button class='btn-run' onclick='runAlgorithm()' id='run-btn'>ИСПОЛНИТЬ ЦИКЛ</button>";
  html += "<button class='btn-clear' onclick='clearQueue()'>ПОЛНЫЙ СБРОС</button>";
  html += "</div>";

  // Скрипты
  html += "<script>";
  html += "let program = []; let isBusy = false; let virtualFingers = [0,0,0,0,0];";
  html += "const fingers = ['БОЛЬШОЙ ПАЛЕЦ', 'УКАЗАТЕЛЬНЫЙ', 'СРЕДНИЙ ПАЛЕЦ', 'БЕЗЫМЯННЫЙ', 'МИЗИНЕЦ', 'ВСЯ КИСТЬ'];";
  html += "const powers = ['Разогнуть', 'Полусжатие', 'Согнуть']; const powers_val = ['0', '50', '100'];";

  // Web Audio FX движок
  html += "const audioCtx = new (window.AudioContext || window.webkitAudioContext)();";
  html += "function playSound(type) {";
  html += "  const osc = audioCtx.createOscillator(); const gain = audioCtx.createGain();";
  html += "  osc.connect(gain); gain.connect(audioCtx.destination);";
  html += "  if(type==='click'){ osc.type='square'; osc.frequency.setValueAtTime(600, audioCtx.currentTime); osc.frequency.exponentialRampToValueAtTime(150, audioCtx.currentTime+0.08); gain.gain.setValueAtTime(0.05, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime+0.08); osc.start(); osc.stop(audioCtx.currentTime+0.08); }";
  html += "  else if(type==='wait'){ osc.type='triangle'; osc.frequency.setValueAtTime(330, audioCtx.currentTime); osc.frequency.setValueAtTime(440, audioCtx.currentTime+0.06); gain.gain.setValueAtTime(0.08, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime+0.15); osc.start(); osc.stop(audioCtx.currentTime+0.15); }";
  html += "  else if(type==='run'){ osc.type='sawtooth'; osc.frequency.setValueAtTime(120, audioCtx.currentTime); osc.frequency.linearRampToValueAtTime(700, audioCtx.currentTime+0.25); gain.gain.setValueAtTime(0.05, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime+0.25); osc.start(); osc.stop(audioCtx.currentTime+0.25); }";
  html += "  else if(type==='alarm'){ osc.type='sawtooth'; osc.frequency.setValueAtTime(900, audioCtx.currentTime); osc.frequency.linearRampToValueAtTime(450, audioCtx.currentTime+0.15); osc.frequency.linearRampToValueAtTime(900, audioCtx.currentTime+0.3); gain.gain.setValueAtTime(0.15, audioCtx.currentTime); gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime+0.3); osc.start(); osc.stop(audioCtx.currentTime+0.3); }";
  html += "}";

  html += "function updateF() { playSound('click'); document.getElementById('f-val').innerText = fingers[document.getElementById('f-slider').value]; }";
  html += "function updateP() { playSound('click'); document.getElementById('p-val').innerText = powers[document.getElementById('p-slider').value]; }";
  html += "function updateW() { playSound('click'); document.getElementById('w-val').innerText = document.getElementById('w-slider').value + ' СЕК'; }";

  html += "function addFingerAction() {";
  html += "  playSound('click'); let f_idx = document.getElementById('f-slider').value;";
  html += "  let p_idx = document.getElementById('p-slider').value;";
  html += "  document.getElementById('placeholder').style.display = 'none';";
  html += "  let id = Date.now();";
  html += "  program.push({id, type: 'motor', finger: f_idx, val: powers_val[p_idx]});";
  html += "  updateUI(fingers[f_idx] + ' : ' + powers[p_idx], 'algo-block', id);";
  html += "}";

  html += "function addWaitAction() {";
  html += "  playSound('wait'); let sec = document.getElementById('w-slider').value;";
  html += "  document.getElementById('placeholder').style.display = 'none';";
  html += "  let id = Date.now();";
  html += "  program.push({id, type: 'wait', val: sec});";
  html += "  updateUI('ПАУЗА ' + sec + ' СЕК', 'algo-block block-wait', id);";
  html += "}";

  html += "function updateUI(text, className, id) {";
  html += "  let div = document.createElement('div'); div.className = className; div.innerText = text;";
  html += "  div.onclick = () => { playSound('click'); program = program.filter(i => i.id !== id); div.remove(); if(!program.length) document.getElementById('placeholder').style.display = 'block'; };";
  html += "  document.getElementById('queue').appendChild(div);";
  html += "}";

  html += "function clearQueue() { playSound('wait'); program = []; virtualFingers = [0,0,0,0,0]; document.getElementById('queue').innerHTML = '<div id=\"placeholder\" style=\"color: #333; width: 100%; margin-top: 35px; font-size: 10px; letter-spacing: 2px; text-align: center;\">ОЖИДАНИЕ КОМАНД...</div>'; }";

  html += "async function runAlgorithm() {";
  html += "  if(isBusy || !program.length) return;";
  html += "  isBusy = true; playSound('run');";
  html += "  const btn = document.getElementById('run-btn'); btn.innerText = 'ВЫПОЛНЕНИЕ...'; btn.style.background = '#222';";
  html += "  for (let cmd of program) {";
  html += "    let res = await fetch(`/execute?f=${cmd.finger || -1}&s=${cmd.val || 0}&t=${cmd.type==='wait' ? cmd.val : 0}`);";
  html += "    let status = await res.text();";
  html += "    if(status === 'FORBIDDEN') { playSound('alarm'); break; }"; // Тихо прерываем без алертов
  html += "    if(status === 'BUSY') { break; }";
  html += "    await new Promise(r => setTimeout(r, cmd.type === 'wait' ? cmd.val * 1000 : 500));";
  html += "  }";
  html += "  isBusy = false; btn.innerText = 'ИСПОЛНИТЬ ЦИКЛ'; btn.style.background = 'var(--danger)'; clearQueue();";
  html += "}";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
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
  int s = server.hasArg("s") ? server.arg("s").toInt() : 0;
  int t = server.hasArg("t") ? server.arg("t").toInt() : 0;

  if(t > 0) {
    isRobotBusy = true; macroStartTime = millis(); macroDuration = t * 1000;
    server.send(200, "text/plain", "OK");
    return;
  }

  int nextFingers[5];
  for(int i=0; i<5; i++) nextFingers[i] = currentFingers[i];

  if (f == 5) {
    for(int i=0; i<5; i++) nextFingers[i] = s;
  } else if (f >= 0 && f <= 4) {
    nextFingers[f] = s;
  }

  // Тихая «математика» блокировок факов
  bool middleIsFree = (nextFingers[2] < 100);
  bool othersAreClenched = (nextFingers[1] == 100 && nextFingers[3] == 100 && nextFingers[4] == 100);
  bool middleAndThumbFree = (nextFingers[2] < 100 && nextFingers[0] < 100);

  if ((middleIsFree && othersAreClenched && nextFingers[0] == 100) || (middleAndThumbFree && nextFingers[1] == 100 && nextFingers[3] == 100 && nextFingers[4] == 100)) {
    // Безопасность: экстренно зажимаем средний палец обратно на Uno
    sendCommandToUno(3, 1);
    currentFingers[2] = 100;
    isRobotBusy = false;
    server.send(200, "text/plain", "FORBIDDEN"); // Сайт поймает это и включит звук сирены
    return;
  }

  isRobotBusy = true; macroStartTime = millis(); macroDuration = 500;

  byte binaryState = (s >= 50) ? 1 : 0;
  if (f == 5) {
    for(int i=0; i<5; i++) currentFingers[i] = s;
    sendCommandToUno(0, binaryState);
  } else if (f >= 0 && f <= 4) {
    currentFingers[f] = s;
    sendCommandToUno(f + 1, binaryState);
  }

  server.send(200, "text/plain", "OK");
}

void handleNotFound() {
  server.sendHeader("Location", "http://10.10.10", true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMask);
  WiFi.softAP(ssid, password);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/execute", handleExecute);
  server.on("/generate_204", handleRoot);
  server.on("/generate204", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}