#include <WiFi.h>
#include <WebServer.h>

///////////////////////////// config 

const char* WIFI_NAME = "your wifi name";
const char* WIFI_PASS = "your wifi password";

// index order everywhere: 0 = left, 1 = right, 2 = stem
const char* NAMES[3]      = {"left", "right", "stem"};
const int   LED_PINS[3]   = {25, 26, 27};
const int   SERVO_PINS[3] = {18, 23, 19};

// servo limits in degrees
const float SERVO_MIN[3]     = {50, 60, 70};
const float SERVO_NEUTRAL[3] = {70, 80, 90};
const float SERVO_MAX[3]     = {90, 100, 110};

// presets for the OPEN / CLOSED buttons
// blade go wrong = swap 
const float SERVO_OPEN[3]   = {85, 65, 105};
const float SERVO_CLOSED[3] = {55, 95, 75};

const float SERVO_STEP = 0.8;   // degrees per tick, lower = slower

////////////////////////////////  state 

enum Mode { MANUAL, BREATHE, CANDLE, WAVE };
const char* MODE_NAMES[] = {"manual", "breathe", "candle", "wave"};
Mode mode = MANUAL;

int   ledLevel[3] = {255, 255, 255};   // full brightness
int   master = 255;                    // overall brightness
float shown[3] = {0, 0, 0};            // actual brightness before master is applied

float candleNow[3]    = {180, 180, 180};
float candleTarget[3] = {180, 180, 180};

float servoNow[3];
float servoTarget[3];

WebServer server(80);
unsigned long lastUpdate = 0;

//////////////////// web page 

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

  /* The triangle edges run at ~41.6deg from vertical, so the side blades
     are rotated ~48.4deg to sit square against them. Each blade's top-centre
     is pinned to the middle of its triangle edge (plus a 4px gap). */
  #blade-stem  { height:130px; left:230px; top:34px; }
  #blade-left  { height:180px; left:187px; top:218px; transform: rotate(48.4deg); }
  #blade-right { height:180px; left:273px; top:218px; transform: rotate(-48.4deg); }

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
  #label-left  { left:40px;  top:360px; }
  #label-right { left:415px; top:360px; }
  #label-master{ left:210px; top:270px; opacity:0.35; }

  .value { display:block; font-size:13px; letter-spacing:1px; opacity:0.9; margin-top:4px; }

  .anims { margin-top:30px; }
  .section-label { font-size:10px; letter-spacing:3px; opacity:0.4; margin-bottom:14px; }

  .servo-block { display:flex; flex-direction:column; align-items:center; gap:20px; }
  .servo-item .label { position:static; display:block; margin-bottom:8px; opacity:0.6; }
  .servo-item .deg { opacity:0.9; margin-left:6px; }
  input[type="range"] { width:220px; accent-color:#fff; }
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
  <div class="label" id="label-master">MASTER<span class="value" id="val-master">100%</span></div>
</div>

<div class="anims">
  <div class="section-label">ANIMATIONS</div>
  <div class="row">
    <button data-mode="breathe" onclick="act('/mode?m=breathe')">BREATHE</button>
    <button data-mode="candle" onclick="act('/mode?m=candle')">CANDLE</button>
    <button data-mode="wave" onclick="act('/mode?m=wave')">WAVE</button>
  </div>
</div>

<div class="anims">
  <div class="section-label">SERVOS</div>
  <div class="row">
    
    <button onclick="act('/servopreset?p=neutral')">NEUTRAL</button>
    
  </div>
  <div class="servo-block">
    <div class="servo-item">
      <div class="label">TILT ALL<span class="deg" id="deg-tilt">0°</span></div>
      <input type="range" min="-30" max="30" step="1" value="0" id="tilt">
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

<script>
const $ = id => document.getElementById(id);
const NAMES = ['left', 'right', 'stem'];

let S = null;          // last state from the lamp
let quietAt = 0;       // don't overwrite controls until this time (user is touching them)
let masterVal = 255;
let angles = [90, 90, 90];
let refreshing = false;
const touch = () => { quietAt = Date.now() + 1200; };

function act(url) {
  return fetch(url)
    .then(r => r.text().then(t => { if (!r.ok) alert(t); refresh(); }))
    .catch(() => {});
}

// sliders fire like crazy, so only keep one request in flight
// and when it finishes send whatever the latest value is
function makeSender(build) {
  let busy = false, next = null;
  async function send(v) {
    if (busy) { next = v; return; }
    busy = true;
    try { await fetch(build(v)); } catch (e) {}
    busy = false;
    if (next !== null) { const n = next; next = null; send(n); }
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

// drag along a blade to set its brightness
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

// drag the triangle up/down for master brightness
(function () {
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

// one slider per servo
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

// tilt all: shifts every servo by the same amount, relative to where they
// were when you grabbed the slider. snaps back to 0 when you let go
const sendServos = makeSender(a => `/servos?l=${a[0]}&r=${a[1]}&s=${a[2]}`);
let snap = null;
const tiltEl = $('tilt');

tiltEl.addEventListener('input', () => {
  touch();
  if (!S) return;
  const tilt = Number(tiltEl.value);
  $('deg-tilt').textContent = tilt + '°';
  if (!snap) snap = angles.slice();

  const a = snap.map((v, i) => Math.max(S.min[i], Math.min(S.max[i], v + tilt)));
  a.forEach((v, i) => {
    angles[i] = v;
    $('servo-' + NAMES[i]).value = v;
    $('deg-' + NAMES[i]).textContent = v.toFixed(1) + '°';
  });
  sendServos(a.map(v => v.toFixed(1)));
});

tiltEl.addEventListener('change', () => {
  snap = null; touch();
  tiltEl.value = 0;
  $('deg-tilt').textContent = '0°';
});

// keep the page in sync with the lamp
function render() {
  if (!S) return;
  document.querySelectorAll('[data-mode]').forEach(b => b.classList.toggle('on', b.dataset.mode === S.mode));

  if (Date.now() < quietAt) return;
  NAMES.forEach((n, i) => setFill(n, S.raw[i]));
  masterVal = S.master; setMasterVisual(masterVal);
  NAMES.forEach((n, i) => {
    const el = $('servo-' + n);
    el.min = S.min[i]; el.max = S.max[i]; el.value = S.servo[i];
    angles[i] = S.servo[i];
    $('deg-' + n).textContent = S.servo[i].toFixed(1) + '°';
  });
}

function refresh() {
  if (refreshing) return;
  refreshing = true;
  fetch('/state').then(r => r.json()).then(s => { S = s; render(); }).catch(() => {}).finally(() => { refreshing = false; });
}

// angles match the blade rotation in the CSS
setupBlade('blade-left',  'left',   48.4);
setupBlade('blade-stem',  'stem',   0);
setupBlade('blade-right', 'right', -48.4);
refresh();
setInterval(refresh, 700);
</script>
</body>
</html>
)rawliteral";

//////////// output helpers

// gamma curve so dimming looks even
void writeLed(int i, float level) {
  float x = constrain(level / 255.0, 0.0, 1.0);
  ledcWrite(LED_PINS[i], pow(x, 2.2) * 4095 + 0.5);
}

// angle -> pulse width (500-2400us) -> 14-bit duty at 50Hz
void writeServo(int i, float angle) {
  angle = constrain(angle, 0.0, 180.0);
  float pulse_us = 500 + (angle / 180.0) * (2400 - 500);
  ledcWrite(SERVO_PINS[i], pulse_us / 20000.0 * 16384 + 0.5);
}

// every servo target goes through this so nothing can leave its limits
float safeAngle(int i, float angle) {
  return constrain(angle, SERVO_MIN[i], SERVO_MAX[i]);
}

int findBlade(const String& name) {
  for (int i = 0; i < 3; i++) if (name == NAMES[i]) return i;
  return -1;
}

// web handlers 

void ok()                      { server.send(200, "text/plain", "ok"); }
void fail(const char* message) { server.send(400, "text/plain", message); }

String listOf(const float* values, int decimals) {
  String s = "[";
  for (int i = 0; i < 3; i++) {
    if (i) s += ",";
    s += String(values[i], decimals);
  }
  return s + "]";
}

// page polls this every 700ms
void handleState() {
  String s = "{\"mode\":\"";
  s += MODE_NAMES[mode];
  s += "\",\"master\":" + String(master);
  s += ",\"raw\":"   + listOf(shown, 0);
  s += ",\"servo\":" + listOf(servoTarget, 1);
  s += ",\"min\":"   + listOf(SERVO_MIN, 1);
  s += ",\"max\":"   + listOf(SERVO_MAX, 1);
  s += "}";
  server.send(200, "application/json", s);
}

void handleLed() {
  int i = findBlade(server.arg("id"));
  if (i < 0) { fail("unknown blade"); return; }

  // if an animation is running, freeze it where it is and switch to manual
  if (mode != MANUAL) {
    for (int k = 0; k < 3; k++) ledLevel[k] = (int)shown[k];
    mode = MANUAL;
  }
  ledLevel[i] = constrain(server.arg("value").toInt(), 0, 255);
  ok();
}

void handleMaster() {
  master = constrain(server.arg("value").toInt(), 0, 255);
  ok();
}

void handleAllOn() {
  mode = MANUAL;
  for (int i = 0; i < 3; i++) ledLevel[i] = 255;
  ok();
}

void handleAllOff() {
  mode = MANUAL;
  for (int i = 0; i < 3; i++) ledLevel[i] = 0;
  ok();
}

void handleMode() {
  String m = server.arg("m");
  if      (m == "breathe") mode = BREATHE;
  else if (m == "candle")  mode = CANDLE;
  else if (m == "wave")    mode = WAVE;
  else { fail("unknown mode"); return; }
  ok();
}

void handleServo() {
  int i = findBlade(server.arg("id"));
  if (i < 0) { fail("unknown servo"); return; }
  servoTarget[i] = safeAngle(i, server.arg("angle").toFloat());
  ok();
}

// all three at once, used by tilt
void handleServos() {
  if (server.hasArg("l")) servoTarget[0] = safeAngle(0, server.arg("l").toFloat());
  if (server.hasArg("r")) servoTarget[1] = safeAngle(1, server.arg("r").toFloat());
  if (server.hasArg("s")) servoTarget[2] = safeAngle(2, server.arg("s").toFloat());
  ok();
}

void handleServoPreset() {
  String p = server.arg("p");
  const float* pose;
  if      (p == "open")    pose = SERVO_OPEN;
  else if (p == "closed")  pose = SERVO_CLOSED;
  else if (p == "neutral") pose = SERVO_NEUTRAL;
  else { fail("unknown preset"); return; }

  for (int i = 0; i < 3; i++) servoTarget[i] = safeAngle(i, pose[i]);
  ok();
}

//LED + servo updates (every 20ms) 

void updateLeds() {
  float t = millis() / 1000.0;

  if (mode == MANUAL) {
    for (int i = 0; i < 3; i++) shown[i] = ledLevel[i];

  } else if (mode == BREATHE) {
    float b = (sin(t * 2.0) * 0.5 + 0.5) * 255;
    for (int i = 0; i < 3; i++) shown[i] = b;

  } else if (mode == CANDLE) {
    for (int i = 0; i < 3; i++) {
      if (random(100) < 15) candleTarget[i] = 140 + random(100);   // new flicker level now and then
      candleNow[i] += (candleTarget[i] - candleNow[i]) * 0.25;     // ease toward it
      shown[i] = candleNow[i];
    }

  } else if (mode == WAVE) {
    float p = t * 2.5;
    shown[2] = (sin(p)        * 0.5 + 0.5) * 255;   // stem
    shown[0] = (sin(p - 2.09) * 0.5 + 0.5) * 255;   // left
    shown[1] = (sin(p - 4.18) * 0.5 + 0.5) * 255;   // right
  }

  for (int i = 0; i < 3; i++) writeLed(i, shown[i] * master / 255.0);
}

// nudge each servo a little toward its target so it moves smoothly
void updateServos() {
  for (int i = 0; i < 3; i++) {
    float diff = servoTarget[i] - servoNow[i];
    if (fabs(diff) <= SERVO_STEP) servoNow[i] = servoTarget[i];
    else if (diff > 0)            servoNow[i] += SERVO_STEP;
    else                          servoNow[i] -= SERVO_STEP;
    writeServo(i, servoNow[i]);
  }
}

/////////////////// setup / loop 

void setup() {
  Serial.begin(115200);

  // LEDs: 5kHz, 12 bit
  for (int i = 0; i < 3; i++) {
    ledcAttach(LED_PINS[i], 5000, 12);
    ledcWrite(LED_PINS[i], 0);
  }

  // servos: 50Hz, start at neutral. delay between them so they don't all jerk at once
  for (int i = 0; i < 3; i++) {
    servoNow[i] = servoTarget[i] = SERVO_NEUTRAL[i];
    ledcAttach(SERVO_PINS[i], 50, 14);
    writeServo(i, servoNow[i]);
    delay(300);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_NAME, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, open http://");
  Serial.println(WiFi.localIP());

  // double blink = we're online
  for (int n = 0; n < 2; n++) {
    for (int i = 0; i < 3; i++) writeLed(i, 120);
    delay(200);
    for (int i = 0; i < 3; i++) writeLed(i, 0);
    delay(300);
  }

  server.on("/",            []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/state",       handleState);
  server.on("/led",         handleLed);
  server.on("/master",      handleMaster);
  server.on("/allon",       handleAllOn);
  server.on("/alloff",      handleAllOff);
  server.on("/mode",        handleMode);
  server.on("/servo",       handleServo);
  server.on("/servos",      handleServos);
  server.on("/servopreset", handleServoPreset);
  server.begin();
}

void loop() {
  server.handleClient();

  if (millis() - lastUpdate >= 20) {
    lastUpdate = millis();
    updateLeds();
    updateServos();
  }
}
