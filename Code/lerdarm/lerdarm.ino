#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

// ================= CONFIG =================
const char* WIFI_SSID = "Fiber";
const char* WIFI_PASS = "16240312";
const char* HOSTNAME  = "lamp";          // reachable as lamp.local
const char* AP_SSID   = "Lamp-Setup";    // fallback hotspot if WiFi fails
const char* AP_PASS   = "lampsetup";     // min 8 chars
const char* OTA_PASS  = "lamp-ota";
const uint32_t WIFI_TIMEOUT_MS = 12000;

// Channel order everywhere: 0 = left, 1 = right, 2 = stem
const char* NAMES[3]   = {"left", "right", "stem"};
const int LED_PIN[3]   = {25, 26, 27};
const int SERVO_PIN[3] = {18, 23, 19};

const int   LED_FREQ = 5000, LED_RES = 12;
const int   LED_MAX  = (1 << LED_RES) - 1;
const float GAMMA    = 2.2f;

const int   SERVO_FREQ = 50, SERVO_RES = 14;
const float SERVO_US_MIN = 500, SERVO_US_MAX = 2400;   // pulse range
const float SERVO_PERIOD_US = 20000;
const uint32_t SERVO_IDLE_MS = 4000;                   // release after this long idle

const int   FRAME_MS = 20;
const float K_STATIC = 0.18f;   // fade speed for manual / off
const float K_ANIM   = 0.30f;   // smoothing for animations

// Safe-by-default servo limits: neutral +/- 20 deg until you calibrate
const float DEF_NEUTRAL[3] = {70, 80, 90};
const float DEF_MARGIN     = 20;

// ================= STATE =================
enum Mode { OFF, MANUAL, BREATHE, CANDLE, WAVE };
const char* MODE_NAMES[] = {"off", "manual", "breathe", "candle", "wave"};
Mode currentMode = MANUAL;

int   baseB[3] = {255, 255, 255};   // per-LED level (0-255), before master
int   masterB  = 127;               // master multiplier (0-255)
float animSpeed = 1.0f;
float raw[3]   = {0, 0, 0};         // level being driven before master
float shown[3] = {0, 0, 0};         // actual output level (after master + fade)
float cand[3]  = {180, 180, 180}, candT[3] = {180, 180, 180};
float breatheT = 0, wavePhase = 0;

bool sleepOn = false;
uint32_t sleepAt = 0;

struct ServoCfg { float mn, ne, mx; };
ServoCfg sc[3];
float sCur[3], sTarget[3];
float pOpen[3], pClosed[3];
uint32_t sLastMove[3] = {0, 0, 0};
bool  sActive[3] = {false, false, false};
float servoSpeed = 40;              // deg/sec
bool  idleRelease = true;
bool  mirrored = true;              // left/right tilt opposite ways (used by SPREAD)
bool  calibMode = false;            // never saved; resets on boot

WebServer server(80);
Preferences prefs;
bool dirty = false; uint32_t dirtyAt = 0;
bool netUp = false;
unsigned long lastFrame = 0;

// ================= HELPERS =================
int chIndex(const String& id) {
  for (int i = 0; i < 3; i++) if (id == NAMES[i]) return i;
  return -1;
}

void markDirty() { dirty = true; dirtyAt = millis(); }

float clampServo(int i, float a) {
  if (calibMode) return constrain(a, 0.0f, 180.0f);
  return constrain(a, sc[i].mn, sc[i].mx);
}

void writeLed(int i, float level) {
  float x = constrain(level / 255.0f, 0.0f, 1.0f);
  ledcWrite(LED_PIN[i], (uint32_t)(powf(x, GAMMA) * LED_MAX + 0.5f));
}

void writeServo(int i, float angle) {
  angle = constrain(angle, 0.0f, 180.0f);
  float us = SERVO_US_MIN + (angle / 180.0f) * (SERVO_US_MAX - SERVO_US_MIN);
  uint32_t duty = (uint32_t)(us / SERVO_PERIOD_US * (1 << SERVO_RES) + 0.5f);
  ledcWrite(SERVO_PIN[i], duty);
  sActive[i] = true;
}

void releaseServo(int i) { ledcWrite(SERVO_PIN[i], 0); sActive[i] = false; }

void applyPreset(const float* p) {
  for (int i = 0; i < 3; i++) sTarget[i] = clampServo(i, p[i]);
}

// ================= PERSISTENCE =================
float getF(const char* p, int i, float d) { char k[12]; snprintf(k, sizeof k, "%s%d", p, i); return prefs.getFloat(k, d); }
void  putF(const char* p, int i, float v) { char k[12]; snprintf(k, sizeof k, "%s%d", p, i); prefs.putFloat(k, v); }

void loadAll() {
  currentMode = (Mode)constrain((int)prefs.getUChar("mode", MANUAL), 0, 4);
  masterB     = prefs.getUChar("master", 127);
  animSpeed   = constrain(prefs.getFloat("aspd", 1.0f), 0.25f, 3.0f);
  servoSpeed  = constrain(prefs.getFloat("sspd", 40.0f), 5.0f, 180.0f);
  idleRelease = prefs.getBool("idle", true);
  mirrored    = prefs.getBool("mir", true);
  for (int i = 0; i < 3; i++) {
    char k[8]; snprintf(k, sizeof k, "b%d", i);
    baseB[i] = prefs.getUChar(k, 255);

    float ne = getF("ne", i, DEF_NEUTRAL[i]);
    float mn = getF("mn", i, ne - DEF_MARGIN);
    float mx = getF("mx", i, ne + DEF_MARGIN);
    if (!(mn >= 0 && mn <= ne && ne <= mx && mx <= 180)) {
      ne = DEF_NEUTRAL[i]; mn = ne - DEF_MARGIN; mx = ne + DEF_MARGIN;
    }
    sc[i] = {mn, ne, mx};
    pOpen[i]   = getF("po", i, ne);
    pClosed[i] = getF("pc", i, ne);
    sCur[i] = sTarget[i] = constrain(getF("pos", i, ne), mn, mx);
  }
}

void saveAll() {
  prefs.putUChar("mode", (uint8_t)currentMode);
  prefs.putUChar("master", (uint8_t)masterB);
  prefs.putFloat("aspd", animSpeed);
  prefs.putFloat("sspd", servoSpeed);
  prefs.putBool("idle", idleRelease);
  prefs.putBool("mir", mirrored);
  for (int i = 0; i < 3; i++) {
    char k[8]; snprintf(k, sizeof k, "b%d", i);
    prefs.putUChar(k, (uint8_t)baseB[i]);
    putF("mn", i, sc[i].mn); putF("ne", i, sc[i].ne); putF("mx", i, sc[i].mx);
    putF("po", i, pOpen[i]); putF("pc", i, pClosed[i]);
    putF("pos", i, sTarget[i]);
  }
  dirty = false;
}

// ================= HTML UI =================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Lamp Control</title>
<style>
  * { box-sizing: border-box; }
  body {
    background:#000; color:#fff;
    font-family:'Courier New', monospace;
    text-align:center; margin:0; padding:40px 20px;
    user-select:none;
  }
  h1 {
    font-size:16px; font-weight:normal; letter-spacing:6px;
    text-transform:uppercase; margin-bottom:40px; opacity:0.8;
  }
  .row { display:flex; flex-wrap:wrap; justify-content:center; gap:14px; margin:16px 0; }

  button {
    background:transparent; color:#fff; border:1px solid #fff;
    padding:14px 22px; font-size:13px; font-family:inherit;
    letter-spacing:2px; cursor:pointer;
    transition: background 0.15s ease, color 0.15s ease;
  }
  button:hover { background:rgba(255,255,255,0.1); }
  button:active { background:#fff; color:#000; }
  button.solid, button.on { background:#fff; color:#000; }
  button.solid:hover { background:#ddd; }
  button.solid:active { background:#aaa; }

  .hub { position:relative; width:500px; max-width:100%; height:400px; margin:60px auto 20px; }

  .blade {
    position:absolute; width:40px; border:1px solid #fff;
    overflow:hidden; background:#000; touch-action:none;
    transform-origin: 50% 0%;
  }
  .fill { position:absolute; bottom:0; left:0; width:100%; background:#fff; height:0%; transition:none; }

  #blade-stem  { height:130px; left:230px; top:34px; }
  #blade-left  { height:180px; left:155px; top:181px; transform: rotate(35deg); }
  #blade-right { height:180px; left:305px; top:181px; transform: rotate(-35deg); }

  .triangle {
    position:absolute; left:170px; top:170px; width:160px; height:90px;
    clip-path: polygon(0 0, 100% 0, 50% 100%);
    background:#fff; opacity:0.12;
    filter: drop-shadow(0 0 0px rgba(255,255,255,0));
    touch-action:none; cursor:pointer;
    transition: opacity 0.05s linear, filter 0.05s linear;
  }

  .label { position:absolute; font-size:11px; letter-spacing:3px; opacity:0.6; }
  #label-stem  { left:210px; top:8px; }
  #label-left  { left:60px;  top:360px; }
  #label-right { left:395px; top:360px; }
  #label-master{ left:210px; top:270px; opacity:0.35; }

  .value { display:block; font-size:13px; letter-spacing:1px; opacity:0.9; margin-top:4px; }

  .anims { margin-top:30px; }
  .section-label { font-size:10px; letter-spacing:3px; opacity:0.4; margin-bottom:14px; }
  .note { font-size:10px; letter-spacing:1px; opacity:0.5; max-width:380px; margin:8px auto; line-height:1.6; }

  .servo-block { display:flex; flex-direction:column; align-items:center; gap:20px; }
  .servo-item .label { position:static; display:block; margin-bottom:8px; opacity:0.6; }
  .servo-item .deg { opacity:0.9; margin-left:6px; }
  input[type="range"] { width:220px; accent-color:#fff; }

  details { margin-top:36px; }
  summary { cursor:pointer; font-size:10px; letter-spacing:3px; opacity:0.5; }
  body:not(.calib) .calonly { display:none; }
  button.warn { border-color:#f55; color:#f55; }
  button.warn.on { background:#f55; color:#000; }
</style>
</head>
<body>
<h1>Lamp Control</h1>

<div class="row">
  <button class="solid" onclick="act('/allon')">ALL ON</button>
  <button onclick="act('/alloff')">ALL OFF</button>
</div>

<div class="hub">
  <div class="blade" id="blade-stem"><div class="fill" id="fill-stem"></div></div>
  <div class="blade" id="blade-left"><div class="fill" id="fill-left"></div></div>
  <div class="blade" id="blade-right"><div class="fill" id="fill-right"></div></div>
  <div class="triangle" id="master"></div>

  <div class="label" id="label-stem">STEM<span class="value" id="val-stem">0%</span></div>
  <div class="label" id="label-left">LEFT<span class="value" id="val-left">0%</span></div>
  <div class="label" id="label-right">RIGHT<span class="value" id="val-right">0%</span></div>
  <div class="label" id="label-master">MASTER<span class="value" id="val-master">50%</span></div>
</div>

<div class="anims">
  <div class="section-label">SCENES</div>
  <div class="row">
    <button onclick="act('/scene?n=reading')">READING</button>
    <button onclick="act('/scene?n=candle')">CANDLE NIGHT</button>
    <button onclick="act('/scene?n=night')">NIGHT</button>
    <button onclick="act('/scene?n=off')">OFF</button>
  </div>
</div>

<div class="anims">
  <div class="section-label">ANIMATIONS</div>
  <div class="row">
    <button data-mode="off" onclick="act('/mode?m=off')">OFF</button>
    <button data-mode="breathe" onclick="act('/mode?m=breathe')">BREATHE</button>
  </div>
  <div class="row">
    <button data-mode="candle" onclick="act('/mode?m=candle')">CANDLE</button>
    <button data-mode="wave" onclick="act('/mode?m=wave')">WAVE</button>
  </div>
  <div class="servo-item">
    <div class="label">SPEED<span class="deg" id="deg-spd">1.0x</span></div>
    <input type="range" id="spd" min="0.25" max="3" step="0.05" value="1">
  </div>
</div>

<div class="anims">
  <div class="section-label">SLEEP TIMER</div>
  <div class="row">
    <button onclick="act('/sleep?min=15')">15 MIN</button>
    <button onclick="act('/sleep?min=30')">30 MIN</button>
    <button onclick="act('/sleep?min=60')">60 MIN</button>
    <button onclick="act('/sleep?min=0')">CANCEL</button>
  </div>
  <div class="note" id="sleep-label"></div>
</div>

<div class="anims">
  <div class="section-label">SERVOS</div>
  <div class="row">
    <button onclick="act('/servopreset?p=closed')">CLOSED</button>
    <button onclick="act('/servopreset?p=neutral')">NEUTRAL</button>
    <button onclick="act('/servopreset?p=open')">OPEN</button>
  </div>
  <div class="servo-block">
    <div class="servo-item">
      <div class="label">TILT ALL<span class="deg" id="deg-tilt">0°</span></div>
      <input type="range" min="-30" max="30" step="1" value="0" id="tilt">
    </div>
    <div class="servo-item">
      <div class="label">SPREAD<span class="deg" id="deg-spread">0°</span></div>
      <input type="range" min="-30" max="30" step="1" value="0" id="spread">
    </div>
    <div class="servo-item">
      <div class="label">LEFT<span class="deg" id="deg-left">--</span></div>
      <input type="range" min="0" max="180" step="0.5" value="90" id="servo-left">
    </div>
    <div class="servo-item">
      <div class="label">STEM<span class="deg" id="deg-stem">--</span></div>
      <input type="range" min="0" max="180" step="0.5" value="90" id="servo-stem">
    </div>
    <div class="servo-item">
      <div class="label">RIGHT<span class="deg" id="deg-right">--</span></div>
      <input type="range" min="0" max="180" step="0.5" value="90" id="servo-right">
    </div>
  </div>
</div>

<details>
  <summary>SERVO CALIBRATION &amp; SETTINGS</summary>
  <div class="servo-block" style="margin-top:20px">
    <div class="servo-item">
      <div class="label">SERVO SPEED<span class="deg" id="deg-sspd">40°/s</span></div>
      <input type="range" min="5" max="120" step="5" value="40" id="sspd">
    </div>
    <div class="row">
      <button id="idle-btn" onclick="act('/servocfg?idle='+(S&&S.idle?0:1))">IDLE RELEASE</button>
      <button id="mir-btn" onclick="act('/servocfg?mirror='+(S&&S.mir?0:1))">MIRRORED</button>
    </div>
    <div class="row">
      <button onclick="act('/savepreset?p=closed')">SAVE POSE AS CLOSED</button>
      <button onclick="act('/savepreset?p=open')">SAVE POSE AS OPEN</button>
    </div>
    <button id="calib-btn" class="warn" onclick="toggleCalib()">CALIBRATION MODE: OFF</button>
    <div class="note calonly">
      Limits are DISABLED. Jog in small steps, stop at the first sign of strain or buzzing.
      Set MIN and MAX first, then NEUTRAL. Turn calibration off when done.
    </div>
    <div id="cal-rows" class="servo-block"></div>
  </div>
</details>

<script>
const $ = id => document.getElementById(id);
const NAMES = ['left', 'right', 'stem'];
let S = null, quietAt = 0, masterVal = 127, angles = [90, 90, 90], refreshing = false;
const touch = () => { quietAt = Date.now() + 1200; };

// ---- helpers ----
function act(url) {
  return fetch(url)
    .then(r => r.text().then(t => { if (!r.ok) alert(t); refresh(); }))
    .catch(() => {});
}

// one request in flight at a time; always ends by sending the latest value
function makeSender(build) {
  let inFlight = false, pending = null;
  async function send(v) {
    if (inFlight) { pending = v; return; }
    inFlight = true;
    try { await fetch(build(v)); } catch (e) {}
    inFlight = false;
    if (pending !== null) { const p = pending; pending = null; send(p); }
  }
  return send;
}

function setFill(name, v) {
  const pct = Math.max(0, Math.min(1, v / 255)) * 100;
  $('fill-' + name).style.height = pct + '%';
  $('val-' + name).textContent = Math.round(pct) + '%';
}

function setMasterVisual(val) {
  const tri = $('master'), pct = val / 255;
  tri.style.opacity = 0.12 + pct * 0.88;
  tri.style.filter = `drop-shadow(0 0 ${pct * 30}px rgba(255,255,255,${0.3 + pct * 0.7}))`;
  $('val-master').textContent = Math.round(pct * 100) + '%';
}

const lim = i => S.calib ? [0, 180] : [S.servo[i].mn, S.servo[i].mx];

// ---- blades (projected onto the blade's own tilted axis) ----
function setupBlade(id, name, angleDeg) {
  const blade = $(id);
  const theta = angleDeg * Math.PI / 180;
  const axisX = -Math.sin(theta), axisY = Math.cos(theta);
  const send = makeSender(v => `/led?id=${name}&value=${v}`);
  let dragging = false, lastVal = -1;

  function update(e) {
    touch();
    const hub = blade.parentElement.getBoundingClientRect();
    const pivotX = hub.left + blade.offsetLeft + blade.offsetWidth / 2;
    const pivotY = hub.top + blade.offsetTop;
    const d = (e.clientX - pivotX) * axisX + (e.clientY - pivotY) * axisY;
    const pct = Math.max(0, Math.min(1, 1 - d / blade.offsetHeight));
    const val = Math.round(pct * 255);
    setFill(name, val);
    if (val !== lastVal) { lastVal = val; send(val); }
  }
  blade.addEventListener('pointerdown', e => { dragging = true; lastVal = -1; blade.setPointerCapture(e.pointerId); update(e); });
  blade.addEventListener('pointermove', e => { if (dragging) update(e); });
  const end = () => { dragging = false; touch(); };
  blade.addEventListener('pointerup', end);
  blade.addEventListener('pointercancel', end);
}

// ---- master triangle ----
(function setupMaster() {
  const tri = $('master');
  const send = makeSender(v => `/master?value=${v}`);
  let dragging = false, startY = 0, startVal = 0, lastVal = -1;
  tri.addEventListener('pointerdown', e => {
    dragging = true; touch(); tri.setPointerCapture(e.pointerId);
    startY = e.clientY; startVal = masterVal; lastVal = -1;
  });
  tri.addEventListener('pointermove', e => {
    if (!dragging) return;
    touch();
    masterVal = Math.max(0, Math.min(255, startVal + (startY - e.clientY) / 200 * 255));
    setMasterVisual(masterVal);
    const v = Math.round(masterVal);
    if (v !== lastVal) { lastVal = v; send(v); }
  });
  const end = () => { dragging = false; touch(); };
  tri.addEventListener('pointerup', end);
  tri.addEventListener('pointercancel', end);
})();

// ---- animation speed ----
(function () {
  const el = $('spd');
  const send = makeSender(v => `/speed?v=${v}`);
  el.addEventListener('input', () => { touch(); $('deg-spd').textContent = Number(el.value).toFixed(2) + 'x'; send(el.value); });
})();

// ---- servo sliders ----
const sendServos = makeSender(a => `/servos?l=${a[0]}&r=${a[1]}&s=${a[2]}`);
NAMES.forEach((n, i) => {
  const el = $('servo-' + n);
  const send = makeSender(v => `/servo?id=${n}&angle=${v}`);
  el.addEventListener('input', () => {
    touch();
    angles[i] = Number(el.value);
    $('deg-' + n).textContent = angles[i].toFixed(1) + '°';
    send(el.value);
  });
});

// TILT ALL / SPREAD: offsets from where the servos were when you grabbed the slider,
// so individual positions are never wiped.
let snap = null;
function applyDelta() {
  if (!S) return;
  const tilt = Number($('tilt').value), spread = Number($('spread').value);
  if (!snap) snap = angles.slice();
  const mir = S.mir ? -1 : 1;
  const d = [tilt + spread, tilt + mir * spread, tilt];
  const a = d.map((v, i) => { const [lo, hi] = lim(i); return Math.max(lo, Math.min(hi, snap[i] + v)); });
  a.forEach((v, i) => {
    angles[i] = v;
    $('servo-' + NAMES[i]).value = v;
    $('deg-' + NAMES[i]).textContent = v.toFixed(1) + '°';
  });
  sendServos(a.map(v => v.toFixed(1)));
}
['tilt', 'spread'].forEach(id => {
  const el = $(id);
  el.addEventListener('input', () => { touch(); $('deg-' + id).textContent = el.value + '°'; applyDelta(); });
  el.addEventListener('change', () => {
    snap = null; touch();
    $('tilt').value = 0; $('spread').value = 0;
    $('deg-tilt').textContent = '0°'; $('deg-spread').textContent = '0°';
  });
});

// ---- servo settings / calibration ----
(function () {
  const el = $('sspd');
  const send = makeSender(v => `/servocfg?speed=${v}`);
  el.addEventListener('input', () => { touch(); $('deg-sspd').textContent = el.value + '°/s'; send(el.value); });
})();

$('cal-rows').innerHTML = NAMES.map(n => `
  <div class="servo-item calonly">
    <div class="label">${n.toUpperCase()}<span class="deg" id="lim-${n}"></span></div>
    <div class="row">
      ${[-5, -1, 1, 5].map(d => `<button onclick="act('/jog?id=${n}&d=${d}')">${d > 0 ? '+' : ''}${d}</button>`).join('')}
    </div>
    <div class="row">
      <button onclick="act('/setlimit?id=${n}&which=min')">SET MIN</button>
      <button onclick="act('/setlimit?id=${n}&which=neutral')">SET NEUTRAL</button>
      <button onclick="act('/setlimit?id=${n}&which=max')">SET MAX</button>
    </div>
  </div>`).join('');

function toggleCalib() {
  if (!S) return;
  if (!S.calib && !confirm('Servo limits will be DISABLED. Move in small steps and stop if a servo strains. Continue?')) return;
  act('/calib?on=' + (S.calib ? 0 : 1));
}

// ---- render state from the lamp ----
function render() {
  if (!S) return;
  document.body.classList.toggle('calib', !!S.calib);
  document.querySelectorAll('[data-mode]').forEach(b => b.classList.toggle('on', b.dataset.mode === S.mode));
  $('calib-btn').textContent = 'CALIBRATION MODE: ' + (S.calib ? 'ON' : 'OFF');
  $('calib-btn').classList.toggle('on', !!S.calib);
  $('idle-btn').textContent = 'IDLE RELEASE: ' + (S.idle ? 'ON' : 'OFF');
  $('mir-btn').textContent = 'MIRRORED: ' + (S.mir ? 'ON' : 'OFF');
  $('sleep-label').textContent = S.sleep > 0
    ? 'SLEEP IN ' + Math.floor(S.sleep / 60) + ':' + String(S.sleep % 60).padStart(2, '0') : '';
  NAMES.forEach((n, i) => {
    $('lim-' + n).textContent = `min ${S.servo[i].mn} · neutral ${S.servo[i].ne} · max ${S.servo[i].mx}`;
  });

  if (Date.now() < quietAt) return;   // don't fight the user's hands
  NAMES.forEach((n, i) => setFill(n, S.raw[i]));
  masterVal = S.master; setMasterVisual(masterVal);
  $('spd').value = S.spd; $('deg-spd').textContent = S.spd.toFixed(2) + 'x';
  $('sspd').value = S.sspd; $('deg-sspd').textContent = S.sspd + '°/s';
  NAMES.forEach((n, i) => {
    const el = $('servo-' + n), [lo, hi] = lim(i);
    el.min = lo; el.max = hi; el.value = S.servo[i].t;
    angles[i] = S.servo[i].t;
    $('deg-' + n).textContent = S.servo[i].t.toFixed(1) + '°';
  });
}

function refresh() {
  if (refreshing) return;
  refreshing = true;
  fetch('/state').then(r => r.json()).then(s => { S = s; render(); }).catch(() => {}).finally(() => { refreshing = false; });
}

setupBlade('blade-left',  'left',  35);
setupBlade('blade-stem',  'stem',  0);
setupBlade('blade-right', 'right', -35);
refresh();
setInterval(refresh, 700);
</script>
</body>
</html>
)rawliteral";

// ================= HANDLERS =================
void ok() { server.send(200, "text/plain", "ok"); }
void bad(int code, const char* msg) { server.send(code, "text/plain", msg); }

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleState() {
  String s; s.reserve(700);
  s += "{\"mode\":\""; s += MODE_NAMES[currentMode];
  s += "\",\"master\":"; s += masterB;
  s += ",\"raw\":[";
  for (int i = 0; i < 3; i++) {
    float r = (currentMode == MANUAL || currentMode == OFF) ? (float)baseB[i] : raw[i];
    if (i) s += ",";
    s += (int)roundf(r);
  }
  s += "],\"spd\":";  s += String(animSpeed, 2);
  s += ",\"sspd\":";  s += String(servoSpeed, 0);
  s += ",\"idle\":";  s += idleRelease ? 1 : 0;
  s += ",\"mir\":";   s += mirrored ? 1 : 0;
  s += ",\"calib\":"; s += calibMode ? 1 : 0;
  int left = sleepOn ? (int)((int32_t)(sleepAt - millis()) / 1000) : 0;
  if (left < 0) left = 0;
  s += ",\"sleep\":"; s += left;
  s += ",\"servo\":[";
  for (int i = 0; i < 3; i++) {
    if (i) s += ",";
    s += "{\"t\":" + String(sTarget[i], 1) + ",\"mn\":" + String(sc[i].mn, 1) +
         ",\"ne\":" + String(sc[i].ne, 1) + ",\"mx\":" + String(sc[i].mx, 1) + "}";
  }
  s += "]}";
  server.send(200, "application/json", s);
}

void handleLed() {
  int i = chIndex(server.arg("id"));
  if (i < 0) { bad(400, "bad id"); return; }
  int v = constrain(server.arg("value").toInt(), 0, 255);
  if (currentMode != MANUAL) {                 // keep what you were seeing, then take over
    for (int k = 0; k < 3; k++) baseB[k] = (int)roundf(raw[k]);
    currentMode = MANUAL;
  }
  baseB[i] = v;
  markDirty(); ok();
}

void handleMaster() {
  masterB = constrain(server.arg("value").toInt(), 0, 255);   // true multiplier, never overwrites blades
  markDirty(); ok();
}

void handleAllOn()  { currentMode = MANUAL; for (int i = 0; i < 3; i++) baseB[i] = 255; markDirty(); ok(); }
void handleAllOff() { currentMode = MANUAL; for (int i = 0; i < 3; i++) baseB[i] = 0;   markDirty(); ok(); }

void handleMode() {
  String m = server.arg("m");
  if (m == "off") currentMode = OFF;
  else if (m == "breathe") currentMode = BREATHE;
  else if (m == "candle") currentMode = CANDLE;
  else if (m == "wave") currentMode = WAVE;
  else { bad(400, "bad mode"); return; }
  markDirty(); ok();
}

void handleSpeed() {
  animSpeed = constrain(server.arg("v").toFloat(), 0.25f, 3.0f);
  markDirty(); ok();
}

void handleSleep() {
  int m = server.arg("min").toInt();
  if (m <= 0) sleepOn = false;
  else { sleepOn = true; sleepAt = millis() + (uint32_t)m * 60000UL; }
  ok();
}

bool applyScene(const String& n) {
  float nz[3] = {sc[0].ne, sc[1].ne, sc[2].ne};
  if (n == "reading") {
    currentMode = MANUAL; for (int i = 0; i < 3; i++) baseB[i] = 255;
    masterB = 230; applyPreset(pOpen);
  } else if (n == "candle") {
    currentMode = CANDLE; masterB = 90; applyPreset(nz);
  } else if (n == "night") {
    currentMode = MANUAL; for (int i = 0; i < 3; i++) baseB[i] = 255;
    masterB = 25; applyPreset(pClosed);
  } else if (n == "off") {
    currentMode = OFF; sleepOn = false; applyPreset(pClosed);
  } else return false;
  markDirty();
  return true;
}
void handleScene() { if (applyScene(server.arg("n"))) ok(); else bad(404, "unknown scene"); }

void handleServo() {
  int i = chIndex(server.arg("id"));
  if (i < 0) { bad(400, "bad id"); return; }
  sTarget[i] = clampServo(i, server.arg("angle").toFloat());
  markDirty(); ok();
}

void handleServos() {
  if (server.hasArg("l")) sTarget[0] = clampServo(0, server.arg("l").toFloat());
  if (server.hasArg("r")) sTarget[1] = clampServo(1, server.arg("r").toFloat());
  if (server.hasArg("s")) sTarget[2] = clampServo(2, server.arg("s").toFloat());
  markDirty(); ok();
}

void handleServoPreset() {
  String p = server.arg("p");
  float nz[3] = {sc[0].ne, sc[1].ne, sc[2].ne};
  if (p == "open") applyPreset(pOpen);
  else if (p == "closed") applyPreset(pClosed);
  else if (p == "neutral") applyPreset(nz);
  else { bad(400, "bad preset"); return; }
  markDirty(); ok();
}

void handleSavePreset() {
  String p = server.arg("p");
  float* dst = (p == "open") ? pOpen : (p == "closed") ? pClosed : nullptr;
  if (!dst) { bad(400, "bad preset"); return; }
  for (int i = 0; i < 3; i++) dst[i] = sTarget[i];
  markDirty(); ok();
}

void handleServoCfg() {
  if (server.hasArg("speed"))  servoSpeed  = constrain(server.arg("speed").toFloat(), 5.0f, 180.0f);
  if (server.hasArg("idle"))   idleRelease = server.arg("idle").toInt() != 0;
  if (server.hasArg("mirror")) mirrored    = server.arg("mirror").toInt() != 0;
  if (!idleRelease) for (int i = 0; i < 3; i++) writeServo(i, sCur[i]);   // re-energise if it was released
  markDirty(); ok();
}

void handleCalib() {
  calibMode = server.arg("on").toInt() != 0;
  if (!calibMode) for (int i = 0; i < 3; i++) sTarget[i] = clampServo(i, sTarget[i]);
  ok();
}

void handleJog() {
  if (!calibMode) { bad(403, "Enable calibration mode first"); return; }
  int i = chIndex(server.arg("id"));
  if (i < 0) { bad(400, "bad id"); return; }
  float d = constrain(server.arg("d").toFloat(), -10.0f, 10.0f);
  sTarget[i] = clampServo(i, sTarget[i] + d);
  markDirty(); ok();
}

void handleSetLimit() {
  if (!calibMode) { bad(403, "Enable calibration mode first"); return; }
  int i = chIndex(server.arg("id"));
  if (i < 0) { bad(400, "bad id"); return; }
  String w = server.arg("which");
  float a = roundf(sTarget[i] * 10.0f) / 10.0f;
  if (w == "min") {
    if (a > sc[i].ne) { bad(400, "MIN must be <= NEUTRAL (set NEUTRAL first, or jog lower)"); return; }
    sc[i].mn = a;
  } else if (w == "max") {
    if (a < sc[i].ne) { bad(400, "MAX must be >= NEUTRAL (set NEUTRAL first, or jog higher)"); return; }
    sc[i].mx = a;
  } else if (w == "neutral") {
    if (a < sc[i].mn || a > sc[i].mx) { bad(400, "NEUTRAL must be between MIN and MAX"); return; }
    sc[i].ne = a;
  } else { bad(400, "bad limit"); return; }
  markDirty(); ok();
}

// ================= ANIMATION / SERVO LOOP =================
void runAnimation() {
  switch (currentMode) {
    case OFF:
    case MANUAL:
      for (int i = 0; i < 3; i++) raw[i] = baseB[i];
      break;
    case BREATHE: {
      breatheT = fmodf(breatheT + 0.03f * animSpeed, TWO_PI);
      float b = (sinf(breatheT) * 0.5f + 0.5f) * 255.0f;
      for (int i = 0; i < 3; i++) raw[i] = b;
      break;
    }
    case CANDLE: {
      int chance = constrain((int)(15 * animSpeed), 1, 95);
      for (int i = 0; i < 3; i++) {
        if (random(0, 100) < chance) candT[i] = 140 + random(0, 100);
        cand[i] += (candT[i] - cand[i]) * 0.25f;
        raw[i] = cand[i];
      }
      break;
    }
    case WAVE: {
      wavePhase = fmodf(wavePhase + 0.05f * animSpeed, TWO_PI);
      raw[2] = (sinf(wavePhase)           * 0.5f + 0.5f) * 255.0f;   // stem
      raw[0] = (sinf(wavePhase - 2.09f)   * 0.5f + 0.5f) * 255.0f;   // left
      raw[1] = (sinf(wavePhase - 4.18f)   * 0.5f + 0.5f) * 255.0f;   // right
      break;
    }
  }

  float k = (currentMode == MANUAL || currentMode == OFF) ? K_STATIC : K_ANIM;
  float m = masterB / 255.0f;
  for (int i = 0; i < 3; i++) {
    float target = (currentMode == OFF) ? 0.0f : raw[i] * m;
    float d = target - shown[i];
    shown[i] = (fabsf(d) < 0.05f) ? target : shown[i] + d * k;
    writeLed(i, shown[i]);
  }
}

void updateServo(int i) {
  float d = sTarget[i] - sCur[i], ad = fabsf(d);
  if (ad < 0.05f) {
    if (ad > 0.0f) { sCur[i] = sTarget[i]; writeServo(i, sCur[i]); sLastMove[i] = millis(); }
    else if (idleRelease && sActive[i] && millis() - sLastMove[i] > SERVO_IDLE_MS) releaseServo(i);
    return;
  }
  float maxStep = servoSpeed * FRAME_MS / 1000.0f;
  float step = ad * 0.15f;                 // ease out near the target
  if (step > maxStep) step = maxStep;
  if (step < 0.08f)   step = 0.08f;
  if (step > ad)      step = ad;
  sCur[i] += (d > 0) ? step : -step;
  writeServo(i, sCur[i]);
  sLastMove[i] = millis();
}

void frameTick() {
  if (millis() - lastFrame < FRAME_MS) return;
  lastFrame = millis();
  runAnimation();
  for (int i = 0; i < 3; i++) updateServo(i);
}

// ================= NETWORK =================
void flickerOnConnect() {
  const float dim = 120;   // logical level (gamma-corrected on output)
  for (int n = 0; n < 2; n++) {
    for (int i = 0; i < 3; i++) writeLed(i, dim);
    delay(200);
    for (int i = 0; i < 3; i++) writeLed(i, 0);
    delay(300);
  }
  delay(500);
}

void startNetServices() {
  ArduinoOTA.setHostname(HOSTNAME);          // also publishes HOSTNAME.local
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([]() {
    for (int i = 0; i < 3; i++) { releaseServo(i); writeLed(i, 0); }
  });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
  netUp = true;
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  prefs.begin("lamp", false);
  loadAll();

  // LEDs on channels 4,5,6
  for (int i = 0; i < 3; i++) {
    ledcAttachChannel(LED_PIN[i], LED_FREQ, LED_RES, 4 + i);
    ledcWrite(LED_PIN[i], 0);
  }

  // Servos on channels 0,1,2
  for (int i = 0; i < 3; i++) {
    bool okAttach = ledcAttachChannel(SERVO_PIN[i], SERVO_FREQ, SERVO_RES, i);
    Serial.printf("servo %d attach: %d\n", i, okAttach);
    writeServo(i, sCur[i]);
    sLastMove[i] = millis();
    delay(300);
  } 

  // WiFi with timeout + hotspot fallback
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
    delay(250); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP address: "); Serial.println(WiFi.localIP());
    startNetServices();
    flickerOnConnect();
  } else {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("WiFi failed. Hotspot '"); Serial.print(AP_SSID);
    Serial.print("' at "); Serial.println(WiFi.softAPIP());
  }

  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/led", handleLed);
  server.on("/master", handleMaster);
  server.on("/allon", handleAllOn);
  server.on("/alloff", handleAllOff);
  server.on("/mode", handleMode);
  server.on("/speed", handleSpeed);
  server.on("/sleep", handleSleep);
  server.on("/scene", handleScene);
  server.on("/servo", handleServo);
  server.on("/servos", handleServos);
  server.on("/servopreset", handleServoPreset);
  server.on("/savepreset", handleSavePreset);
  server.on("/servocfg", handleServoCfg);
  server.on("/calib", handleCalib);
  server.on("/jog", handleJog);
  server.on("/setlimit", handleSetLimit);
  server.begin();
}

void loop() {
  server.handleClient();
  if (netUp) ArduinoOTA.handle();
  frameTick();

  if (sleepOn && (int32_t)(millis() - sleepAt) >= 0) { sleepOn = false; applyScene("off"); }
  if (dirty && millis() - dirtyAt > 2000) saveAll();

  static uint32_t lastNetCheck = 0;
  if (millis() - lastNetCheck > 10000) {
    lastNetCheck = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    else if (!netUp) startNetServices();
  }
}