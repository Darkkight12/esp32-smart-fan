/*
 * Smart Fan - ESP32, 3 relays, potentiometer, local web UI + Sinric Pro (Google Home / Alexa)
 *
 * Libraries (Library Manager):
 *   - SinricPro            (v5.x, needs Arduino-ESP32 core 3.x)
 *   - ArduinoJson          (>= 7.0.3)   <- required by SinricPro
 *   - WebSockets by Markus Sattler (>= 2.4.0) <- required by SinricPro
 *   - WiFiManager by tzapu
 *
 * Sinric Pro portal: create a device from the built-in "Fan" template (NOT a custom one),
 * then link Sinric Pro in the Google Home app.
 *
 * Sinric App Key / App Secret / Device ID and the web PIN are entered in the
 * "SmartFan-Setup" Wi-Fi portal and stored in flash (nothing hardcoded).
 * To change them later: open the web UI -> "Change Wi-Fi / Setup".
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// Our own mDNS name (smartfan.local) is used, so stop the SDK announcing its own.
#define SINRICPRO_NOMDNS
#include <SinricPro.h>
#include <SinricProFan.h>

// =========================
// SETTINGS
// =========================

const char* MDNS_NAME = "smartfan";

#define MAX_SESSIONS        8
#define SESSION_LIFETIME_MS (24UL * 60UL * 60UL * 1000UL)  // 24 h
#define MAX_FAILED_LOGINS   5
#define LOCKOUT_MS          60000UL

// Relay pins
#define RELAY_SPEED1 25
#define RELAY_SPEED2 26
#define RELAY_SPEED3 27

#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// Break-before-make delay between speed taps (ms)
#define SWITCH_DELAY_MS 250

// Potentiometer
#define POT_PIN 34
#define POT_HYSTERESIS 120   // ADC counts of dead-band around each zone boundary

// =========================
// VARIABLES
// =========================

WebServer server(80);
Preferences prefs;

int currentSpeed = 0;
int lastOnSpeed  = 1;     // speed restored when "turn on" arrives without a level
int potZone      = -1;

bool timerActive = false;
unsigned long fanTimerEnd = 0;

bool servicesStarted = false;
unsigned long lastWifiOk = 0;

// Stored configuration
String cfgAppKey, cfgAppSecret, cfgDeviceId, cfgPin;
bool shouldSaveConfig = false;

// Login throttling
int failedLogins = 0;
bool lockActive = false;
unsigned long lockUntil = 0;

// Sinric Pro
SinricProFan* sinricFan = nullptr;
bool sinricConfigured = false;
bool sinricConnected  = false;
bool sinricDirty      = false;   // state changed -> tell the cloud
unsigned long lastSinricEvent = 0;

// =========================
// SESSION STORAGE
// =========================

struct Session {
  String token;
  bool active;
  unsigned long created;
};

Session sessions[MAX_SESSIONS];

// =========================
// FAN CONTROL
// =========================

void allRelaysOff() {
  digitalWrite(RELAY_SPEED1, RELAY_OFF);
  digitalWrite(RELAY_SPEED2, RELAY_OFF);
  digitalWrite(RELAY_SPEED3, RELAY_OFF);
}

// Low-level relay switching (break-before-make). Does nothing if speed is unchanged.
void setFan(int speed) {
  if (speed == currentSpeed) return;

  allRelaysOff();
  delay(SWITCH_DELAY_MS);

  if (speed == 1)      digitalWrite(RELAY_SPEED1, RELAY_ON);
  else if (speed == 2) digitalWrite(RELAY_SPEED2, RELAY_ON);
  else if (speed == 3) digitalWrite(RELAY_SPEED3, RELAY_ON);

  currentSpeed = speed;
}

// =========================
// POTENTIOMETER
// =========================

int readPotAveraged() {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(POT_PIN);
  return (int)(sum / 8);
}

// Zone with hysteresis so noise near a boundary does not flap the relays
int zoneFromValue(int v, int prev) {
  static const int lo[4] = {0, 1024, 2048, 3072};
  static const int hi[4] = {1024, 2048, 3072, 4096};

  if (prev >= 0 && v >= lo[prev] - POT_HYSTERESIS && v < hi[prev] + POT_HYSTERESIS) {
    return prev;
  }
  if (v < 1024) return 0;
  if (v < 2048) return 1;
  if (v < 3072) return 2;
  return 3;
}

// Single entry point for every speed change (pot, web, timer, voice)
void applySpeed(int speed) {
  speed = constrain(speed, 0, 3);

  if (speed > 0) lastOnSpeed = speed;

  setFan(speed);

  if (speed == 0) timerActive = false;

  // Pot only takes over again when it is physically moved to another zone
  potZone = zoneFromValue(readPotAveraged(), potZone);

  sinricDirty = true;
}

void readPotentiometer() {
  int zone = zoneFromValue(readPotAveraged(), potZone);

  if (zone != potZone) {
    potZone = zone;
    applySpeed(zone);
  }
}

// =========================
// SINRIC PRO (GOOGLE HOME)
// =========================

bool onPowerState(const String& deviceId, bool& state) {
  Serial.printf("[Sinric] power %s\r\n", state ? "ON" : "OFF");

  if (state) {
    applySpeed(currentSpeed > 0 ? currentSpeed : lastOnSpeed);
  } else {
    applySpeed(0);
  }
  return true;
}

// Fan speed 1..3 ("set the fan speed to 2", "low/medium/high")
bool onRangeValue(const String& deviceId, int& rangeValue) {
  rangeValue = constrain(rangeValue, 1, 3);
  Serial.printf("[Sinric] speed %d\r\n", rangeValue);

  applySpeed(rangeValue);
  return true;
}

// "increase / decrease fan speed" - reply with the new absolute value
bool onAdjustRangeValue(const String& deviceId, int& rangeValueDelta) {
  int base = currentSpeed > 0 ? currentSpeed : 0;
  int target = constrain(base + rangeValueDelta, 1, 3);
  Serial.printf("[Sinric] adjust %d -> %d\r\n", rangeValueDelta, target);

  applySpeed(target);
  rangeValueDelta = target;
  return true;
}

void startSinric() {
  if (cfgAppKey.length() < 10 || cfgAppSecret.length() < 10 || cfgDeviceId.length() < 10) {
    Serial.println("Sinric Pro not configured (open Setup portal to add credentials)");
    return;
  }

  SinricProFan& fan = SinricPro[cfgDeviceId];
  sinricFan = &fan;

  fan.onPowerState(onPowerState);
  fan.onRangeValue(onRangeValue);
  fan.onAdjustRangeValue(onAdjustRangeValue);

  SinricPro.onConnected([]() {
    sinricConnected = true;
    sinricDirty = true;          // push the real state on every (re)connect
    Serial.println("[Sinric] connected");
  });
  SinricPro.onDisconnected([]() {
    sinricConnected = false;
    Serial.println("[Sinric] disconnected");
  });

  SinricPro.begin(cfgAppKey, cfgAppSecret);
  sinricConfigured = true;
}

// Report physical / web / timer changes so Google Home never shows a stale state
void sinricReportIfNeeded() {
  if (!sinricConfigured || !sinricConnected || !sinricDirty) return;
  if (millis() - lastSinricEvent < 1000) return;   // be gentle with event rate

  lastSinricEvent = millis();
  sinricDirty = false;

  sinricFan->sendPowerStateEvent(currentSpeed > 0);
  if (currentSpeed > 0) {
    sinricFan->sendRangeValueEvent(currentSpeed);
  }
}

// =========================
// RANDOM SESSION TOKEN
// =========================

String generateToken() {
  const char chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789";

  String token = "";
  for (int i = 0; i < 32; i++) {
    token += chars[esp_random() % (sizeof(chars) - 1)];
  }
  return token;
}

// =========================
// COOKIE / AUTH
// =========================

String getCookieToken() {
  if (!server.hasHeader("Cookie")) return "";

  String cookie = server.header("Cookie");
  String key = "SMARTFAN_SESSION=";
  int start = cookie.indexOf(key);
  if (start == -1) return "";

  start += key.length();
  int end = cookie.indexOf(';', start);
  if (end == -1) end = cookie.length();

  return cookie.substring(start, end);
}

bool isAuthenticated() {
  String token = getCookieToken();
  if (token == "") return false;

  unsigned long now = millis();

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].active) continue;

    // Expire old sessions (rollover-safe)
    if ((unsigned long)(now - sessions[i].created) > SESSION_LIFETIME_MS) {
      sessions[i].active = false;
      sessions[i].token = "";
      continue;
    }

    if (sessions[i].token == token) return true;
  }
  return false;
}

String createSession() {
  String token = generateToken();
  unsigned long now = millis();

  int slot = -1;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].active) { slot = i; break; }
  }

  // All full: replace the oldest session
  if (slot == -1) {
    unsigned long oldestAge = 0;
    slot = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
      unsigned long age = now - sessions[i].created;
      if (age >= oldestAge) { oldestAge = age; slot = i; }
    }
  }

  sessions[slot].token = token;
  sessions[slot].active = true;
  sessions[slot].created = now;
  return token;
}

void deleteCurrentSession() {
  String token = getCookieToken();
  if (token == "") return;

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active && sessions[i].token == token) {
      sessions[i].active = false;
      sessions[i].token = "";
      break;
    }
  }
}

// Page routes: redirect to login
bool checkAuthentication() {
  if (isAuthenticated()) return true;
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
  return false;
}

// API routes: 401 so fetch() can handle it properly
bool checkApiAuth() {
  if (isAuthenticated()) return true;
  server.send(401, "application/json", "{\"error\":\"auth\"}");
  return false;
}

bool pinMatches(const String& entered) {
  if (entered.length() != cfgPin.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < entered.length(); i++) {
    diff |= (uint8_t)(entered[i] ^ cfgPin[i]);
  }
  return diff == 0;
}

// =========================
// PAGES (stored in flash)
// =========================

const char LOGIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Fan Login</title>
<style>
*{box-sizing:border-box}
body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;font-family:Arial,sans-serif;color:#fff;
background:radial-gradient(circle at top,#263238,#101417 55%,#080a0c)}
.card{width:90%;max-width:380px;padding:35px;border-radius:28px;background:rgba(255,255,255,.08);
border:1px solid rgba(255,255,255,.12);backdrop-filter:blur(20px);box-shadow:0 25px 60px rgba(0,0,0,.45);text-align:center}
.logo{width:75px;height:75px;margin:0 auto 20px;border-radius:50%;display:flex;align-items:center;justify-content:center;
font-size:22px;font-weight:bold;background:rgba(255,255,255,.1)}
h1{margin:0 0 8px}.sub{color:#aab4b9;margin-bottom:30px}
input{width:100%;padding:15px;border-radius:14px;border:1px solid rgba(255,255,255,.15);background:rgba(0,0,0,.25);
color:#fff;font-size:18px;text-align:center;outline:none}
button{width:100%;margin-top:15px;padding:15px;border:none;border-radius:14px;background:#fff;color:#111;
font-size:16px;font-weight:bold;cursor:pointer}
button:active{transform:scale(.98)}
</style></head><body>
<div class="card">
<div class="logo">FAN</div>
<h1>Smart Fan</h1>
<div class="sub">Enter PIN to continue</div>
<form method="POST" action="/login">
<input type="password" name="pin" inputmode="numeric" maxlength="10" placeholder="PIN" autofocus>
<button type="submit">Unlock</button>
</form></div></body></html>
)rawliteral";

const char ROOT_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Fan</title>
<style>
*{box-sizing:border-box}
body{margin:0;min-height:100vh;font-family:Arial,Helvetica,sans-serif;color:#fff;padding:20px;
background:radial-gradient(circle at top left,#263238,#101417 50%,#080a0c)}
.container{width:100%;max-width:700px;margin:auto}
.header{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px}
.brand{display:flex;align-items:center;gap:12px}
.brand-icon{width:48px;height:48px;border-radius:15px;display:flex;align-items:center;justify-content:center;
background:rgba(255,255,255,.1);font-size:13px;font-weight:bold}
.brand h1{margin:0;font-size:22px}
.brand p{margin:3px 0 0;color:#89969c;font-size:12px}
.online{display:flex;align-items:center;gap:7px;padding:8px 12px;border-radius:20px;background:rgba(255,255,255,.07);
font-size:12px;color:#b9c4c8}
.dot{width:8px;height:8px;border-radius:50%;background:#49d17d;box-shadow:0 0 10px #49d17d}
.dot.off{background:#e5534b;box-shadow:0 0 10px #e5534b}
.card{margin-bottom:18px;padding:22px;border-radius:25px;background:rgba(255,255,255,.075);
border:1px solid rgba(255,255,255,.1);backdrop-filter:blur(20px);box-shadow:0 20px 50px rgba(0,0,0,.25)}
.fan-area{text-align:center;padding:15px 10px 25px}
.fan{width:190px;height:190px;margin:5px auto 25px;position:relative;border-radius:50%;display:flex;align-items:center;
justify-content:center;background:radial-gradient(circle,rgba(255,255,255,.12),rgba(255,255,255,.035));
border:2px solid rgba(255,255,255,.15);box-shadow:inset 0 0 30px rgba(255,255,255,.04),0 20px 50px rgba(0,0,0,.25)}
.fan::before,.fan::after{content:"";position:absolute;border-radius:50%;border:1px solid rgba(255,255,255,.07)}
.fan::before{width:155px;height:155px}.fan::after{width:120px;height:120px}
.fan-blades{width:145px;height:145px;position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);border-radius:50%}
.blade{position:absolute;width:43px;height:78px;left:51px;top:3px;transform-origin:21px 69px;border-radius:65% 35% 55% 45%;
background:linear-gradient(135deg,rgba(255,255,255,.6),rgba(255,255,255,.12));box-shadow:inset 2px 2px 8px rgba(255,255,255,.15)}
.blade:nth-child(2){transform:rotate(90deg)}.blade:nth-child(3){transform:rotate(180deg)}.blade:nth-child(4){transform:rotate(270deg)}
.fan-center{position:absolute;width:34px;height:34px;left:50%;top:50%;transform:translate(-50%,-50%);border-radius:50%;
background:radial-gradient(circle at 35% 30%,#fff,#89969c 45%,#3b464b 100%);border:3px solid rgba(255,255,255,.2);
z-index:5;box-shadow:0 3px 12px rgba(0,0,0,.5)}
.fan.speed1 .fan-blades{animation:spin 2.2s linear infinite}
.fan.speed2 .fan-blades{animation:spin 1.15s linear infinite}
.fan.speed3 .fan-blades{animation:spin .55s linear infinite}
@keyframes spin{from{transform:translate(-50%,-50%) rotate(0)}to{transform:translate(-50%,-50%) rotate(360deg)}}
.speed-text{font-size:30px;font-weight:bold;margin-bottom:5px}
.description{color:#8e9ba0;font-size:14px}
.section-title{font-size:14px;color:#9ca8ac;margin-bottom:13px}
.speed-buttons{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}
.speed-button{padding:15px 5px;border:1px solid rgba(255,255,255,.08);border-radius:17px;background:rgba(255,255,255,.05);
color:#fff;cursor:pointer;transition:.2s}
.speed-button:hover{background:rgba(255,255,255,.1)}
.speed-button.active{background:#fff;color:#111;box-shadow:0 8px 25px rgba(255,255,255,.15)}
.speed-number{font-size:20px;font-weight:bold}
.speed-label{font-size:10px;margin-top:5px;opacity:.65}
.timer-row{display:flex;gap:10px}
.timer-row input{flex:1;min-width:0;padding:14px;border-radius:14px;border:1px solid rgba(255,255,255,.1);
background:rgba(0,0,0,.2);color:#fff;outline:none;font-size:15px}
.timer-row button{padding:14px 20px;border:none;border-radius:14px;background:#fff;color:#111;font-weight:bold;cursor:pointer}
.timer-status{margin-top:12px;color:#89969c;font-size:13px}
.info-row{display:flex;justify-content:space-between;padding:13px 0;border-bottom:1px solid rgba(255,255,255,.06);font-size:14px}
.info-row:last-child{border-bottom:none}
.info-label{color:#89969c}
.actions{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.action-button{padding:14px;border-radius:14px;border:1px solid rgba(255,255,255,.1);background:rgba(255,255,255,.06);
color:#fff;text-decoration:none;text-align:center;cursor:pointer;font-size:14px;font-family:inherit}
.action-button:hover{background:rgba(255,255,255,.11)}
.logout{color:#ff8585}
@media(max-width:500px){
body{padding:12px}.card{padding:17px}.fan{width:170px;height:170px}
.speed-buttons{gap:6px}.speed-button{padding:12px 3px}.actions{grid-template-columns:1fr}}
</style></head><body>
<div class="container">

<div class="header">
  <div class="brand">
    <div class="brand-icon">FAN</div>
    <div><h1>Smart Fan</h1><p>Local Fan Controller</p></div>
  </div>
  <div class="online"><span id="onlineDot" class="dot"></span><span id="onlineText">Online</span></div>
</div>

<div class="card fan-area">
  <div id="fan" class="fan">
    <div class="fan-blades"><div class="blade"></div><div class="blade"></div><div class="blade"></div><div class="blade"></div></div>
    <div class="fan-center"></div>
  </div>
  <div id="speedText" class="speed-text">OFF</div>
  <div id="description" class="description">Fan is stopped</div>
</div>

<div class="card">
  <div class="section-title">FAN SPEED</div>
  <div class="speed-buttons">
    <button id="speed0" class="speed-button" onclick="setSpeed(0)"><div class="speed-number">OFF</div><div class="speed-label">Stop</div></button>
    <button id="speed1" class="speed-button" onclick="setSpeed(1)"><div class="speed-number">1</div><div class="speed-label">Low</div></button>
    <button id="speed2" class="speed-button" onclick="setSpeed(2)"><div class="speed-number">2</div><div class="speed-label">Medium</div></button>
    <button id="speed3" class="speed-button" onclick="setSpeed(3)"><div class="speed-number">3</div><div class="speed-label">High</div></button>
  </div>
</div>

<div class="card">
  <div class="section-title">TIMER</div>
  <div class="timer-row">
    <input id="timerInput" type="number" min="1" max="1440" placeholder="Minutes">
    <button onclick="setTimer()">Start</button>
    <button onclick="cancelTimer()" style="background:rgba(255,255,255,.12);color:#fff">Cancel</button>
  </div>
  <div id="timerStatus" class="timer-status">Timer inactive</div>
</div>

<div class="card">
  <div class="section-title">DEVICE</div>
  <div class="info-row"><span class="info-label">Address</span><span>smartfan.local</span></div>
  <div class="info-row"><span class="info-label">Connection</span><span>Local Wi-Fi</span></div>
  <div class="info-row"><span class="info-label">Voice (Google Home)</span><span id="voiceStatus">-</span></div>
  <div class="info-row"><span class="info-label">Security</span><span>PIN Protected</span></div>
</div>

<div class="card">
  <div class="actions">
    <button class="action-button" onclick="resetWifi()">Change Wi-Fi / Setup</button>
    <a class="action-button logout" href="/logout">Logout</a>
  </div>
</div>

</div>

<script>
const fan = document.getElementById("fan");
const speedText = document.getElementById("speedText");
const description = document.getElementById("description");
const timerStatus = document.getElementById("timerStatus");
let timerSeconds = 0;

const NAMES = ["OFF", "SPEED 1", "SPEED 2", "SPEED 3"];
const DESC  = ["Fan is stopped", "Low speed", "Medium speed", "High speed"];

function api(path) {
  return fetch(path, { method: "POST" }).then(r => {
    if (r.status === 401 && path !== "/status") { location = "/login"; throw new Error("auth"); }
    return r;
  });
}

function updateFan(speed) {
  fan.classList.remove("speed1", "speed2", "speed3");
  for (let i = 0; i <= 3; i++) document.getElementById("speed" + i).classList.remove("active");
  speedText.innerText = NAMES[speed];
  description.innerText = DESC[speed];
  if (speed > 0) fan.classList.add("speed" + speed);
  document.getElementById("speed" + speed).classList.add("active");
}

function setSpeed(speed) {
  api("/speed?value=" + speed).then(r => {
    if (!r.ok) throw new Error();
    updateFan(speed);
  }).catch(e => { if (e.message !== "auth") alert("Failed to change fan speed"); });
}

function setTimer() {
  const minutes = document.getElementById("timerInput").value;
  if (minutes === "" || Number(minutes) <= 0) { alert("Enter a valid time"); return; }
  api("/timer?minutes=" + minutes).then(r => r.text().then(t => {
    if (!r.ok) { alert(t || "Failed to start timer"); return; }
    document.getElementById("timerInput").value = "";
    updateStatus();
  })).catch(e => { if (e.message !== "auth") alert("Failed to start timer"); });
}

function cancelTimer() {
  api("/timer?minutes=0").then(updateStatus).catch(() => {});
}

function resetWifi() {
  if (!confirm("Clear Wi-Fi settings and restart into setup mode?\nYou can also update Sinric Pro credentials and the PIN there.")) return;
  api("/resetwifi").then(() => alert("Restarting. Connect to the 'SmartFan-Setup' Wi-Fi network to continue."));
}

function formatTime(s) {
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), x = s % 60;
  const p = n => String(n).padStart(2, "0");
  return (h > 0 ? h + ":" : "") + p(m) + ":" + p(x);
}

function setOnline(ok) {
  document.getElementById("onlineDot").className = "dot" + (ok ? "" : " off");
  document.getElementById("onlineText").innerText = ok ? "Online" : "Offline";
}

function updateStatus() {
  api("/status").then(r => {
    if (r.status === 401) { location = "/login"; throw new Error("auth"); }
    return r.json();
  }).then(d => {
    setOnline(true);
    updateFan(d.speed);
    document.getElementById("voiceStatus").innerText =
      d.voice === "connected" ? "Connected" : (d.voice === "offline" ? "Connecting..." : "Not configured");
    if (d.timerActive) {
      timerSeconds = d.timerRemaining;
      timerStatus.innerText = "Fan will turn off in " + formatTime(timerSeconds);
    } else {
      timerSeconds = 0;
      timerStatus.innerText = "Timer inactive";
    }
  }).catch(e => {
    if (e.message !== "auth") { setOnline(false); timerStatus.innerText = "Connection lost"; }
  });
}

setInterval(() => {
  if (timerSeconds > 0) {
    timerSeconds--;
    timerStatus.innerText = "Fan will turn off in " + formatTime(timerSeconds);
  }
}, 1000);

setInterval(updateStatus, 2000);
updateStatus();
</script>
</body></html>
)rawliteral";

void sendMessagePage(int code, const String& title, const String& message) {
  String html =
    "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Smart Fan</title><style>"
    "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
    "background:radial-gradient(circle at top,#263238,#101417 55%,#080a0c);color:#fff;font-family:Arial,sans-serif}"
    ".card{width:90%;max-width:380px;padding:35px;border-radius:28px;background:rgba(255,255,255,.08);text-align:center}"
    ".err{color:#ff6b6b;margin-bottom:20px}a{display:block;margin-top:20px;color:#fff}"
    "</style></head><body><div class=\"card\"><h2>";
  html += title;
  html += "</h2><div class=\"err\">";
  html += message;
  html += "</div><a href=\"/login\">Back</a></div></body></html>";
  server.send(code, "text/html", html);
}

// =========================
// ROUTES
// =========================

void handleLoginPage() {
  if (isAuthenticated()) {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }
  server.send_P(200, "text/html", LOGIN_PAGE);
}

void handleLogin() {
  if (lockActive) {
    if ((long)(lockUntil - millis()) > 0) {
      sendMessagePage(429, "Too many attempts", "Please wait a minute and try again.");
      return;
    }
    lockActive = false;
    failedLogins = 0;
  }

  if (!server.hasArg("pin")) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return;
  }

  if (pinMatches(server.arg("pin"))) {
    failedLogins = 0;

    String token = createSession();
    server.sendHeader("Set-Cookie",
      "SMARTFAN_SESSION=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400");
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  failedLogins++;
  if (failedLogins >= MAX_FAILED_LOGINS) {
    lockActive = true;
    lockUntil = millis() + LOCKOUT_MS;
  }
  delay(500);   // slows down guessing
  sendMessagePage(401, "Incorrect PIN", "The PIN you entered is incorrect.");
}

void handleLogout() {
  deleteCurrentSession();
  server.sendHeader("Set-Cookie", "SMARTFAN_SESSION=deleted; Path=/; Max-Age=0");
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
}

void handleRoot() {
  if (!checkAuthentication()) return;
  server.send_P(200, "text/html", ROOT_PAGE);
}

void handleSpeed() {
  if (!checkApiAuth()) return;

  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing speed value");
    return;
  }

  int speed = server.arg("value").toInt();
  if (speed < 0 || speed > 3) {
    server.send(400, "text/plain", "Speed must be 0 through 3");
    return;
  }

  applySpeed(speed);
  server.send(200, "text/plain", "OK");
}

void handleTimer() {
  if (!checkApiAuth()) return;

  if (!server.hasArg("minutes")) {
    server.send(400, "text/plain", "Missing timer value");
    return;
  }

  long minutes = server.arg("minutes").toInt();

  if (minutes <= 0) {
    timerActive = false;
    server.send(200, "text/plain", "Timer cancelled");
    return;
  }

  if (currentSpeed == 0) {
    server.send(409, "text/plain", "Turn the fan on first");
    return;
  }

  if (minutes > 1440) minutes = 1440;

  fanTimerEnd = millis() + ((unsigned long)minutes * 60000UL);
  timerActive = true;

  server.send(200, "text/plain", "Timer started");
}

void handleStatus() {
  if (!checkApiAuth()) return;

  unsigned long remaining = 0;
  if (timerActive) {
    long diff = (long)(fanTimerEnd - millis());
    if (diff > 0) remaining = diff / 1000;
  }

  const char* voice = !sinricConfigured ? "none" : (sinricConnected ? "connected" : "offline");

  String json = "{\"speed\":";
  json += String(currentSpeed);
  json += ",\"timerActive\":";
  json += timerActive ? "true" : "false";
  json += ",\"timerRemaining\":";
  json += String(remaining);
  json += ",\"voice\":\"";
  json += voice;
  json += "\"}";

  server.send(200, "application/json", json);
}

void handleResetWiFi() {
  if (!checkApiAuth()) return;

  server.send(200, "text/plain", "Wi-Fi settings cleared. Restarting...");
  delay(500);

  WiFiManager wm;
  wm.resetSettings();   // Sinric credentials / PIN stay saved and are pre-filled in the portal

  ESP.restart();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void startWebServer() {
  const char* headerKeys[] = { "Cookie" };
  server.collectHeaders(headerKeys, 1);

  server.on("/login",     HTTP_GET,  handleLoginPage);
  server.on("/login",     HTTP_POST, handleLogin);
  server.on("/logout",    HTTP_GET,  handleLogout);
  server.on("/",          HTTP_GET,  handleRoot);
  server.on("/speed",     HTTP_POST, handleSpeed);
  server.on("/timer",     HTTP_POST, handleTimer);
  server.on("/status",    HTTP_POST, handleStatus);
  server.on("/resetwifi", HTTP_POST, handleResetWiFi);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web server started");
}

void startMDNS() {
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS started: http://smartfan.local");
  } else {
    Serial.println("mDNS failed");
  }
}

// =========================
// CONFIG STORAGE / WIFI
// =========================

void loadConfig() {
  prefs.begin("smartfan", true);
  cfgAppKey    = prefs.getString("appkey", "");
  cfgAppSecret = prefs.getString("appsecret", "");
  cfgDeviceId  = prefs.getString("devid", "");
  cfgPin       = prefs.getString("pin", "1234");
  prefs.end();
}

void saveConfig() {
  prefs.begin("smartfan", false);
  prefs.putString("appkey", cfgAppKey);
  prefs.putString("appsecret", cfgAppSecret);
  prefs.putString("devid", cfgDeviceId);
  prefs.putString("pin", cfgPin);
  prefs.end();
}

void onSaveConfig() {
  shouldSaveConfig = true;
}

void startWiFi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setSaveConfigCallback(onSaveConfig);

  WiFiManagerParameter pKey("appkey", "Sinric App Key", cfgAppKey.c_str(), 64);
  WiFiManagerParameter pSecret("appsecret", "Sinric App Secret", cfgAppSecret.c_str(), 100);
  WiFiManagerParameter pDev("devid", "Sinric Fan Device ID", cfgDeviceId.c_str(), 32);
  WiFiManagerParameter pPin("pin", "Web PIN (4-10 digits)", cfgPin.c_str(), 10);

  wm.addParameter(&pKey);
  wm.addParameter(&pSecret);
  wm.addParameter(&pDev);
  wm.addParameter(&pPin);

  Serial.println("Connecting to Wi-Fi...");

  bool ok = wm.autoConnect("SmartFan-Setup");

  if (shouldSaveConfig) {
    cfgAppKey    = String(pKey.getValue());
    cfgAppSecret = String(pSecret.getValue());
    cfgDeviceId  = String(pDev.getValue());
    String newPin = String(pPin.getValue());
    cfgAppKey.trim(); cfgAppSecret.trim(); cfgDeviceId.trim(); newPin.trim();
    if (newPin.length() >= 4) cfgPin = newPin;
    saveConfig();
    Serial.println("Configuration saved");
  }

  if (!ok) {
    Serial.println("Wi-Fi connection failed (running offline, will retry)");
    return;
  }

  WiFi.setAutoReconnect(true);

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  startMDNS();
  startWebServer();
  startSinric();
  servicesStarted = true;

  if (cfgPin == "1234") {
    Serial.println("WARNING: default PIN in use - change it in the setup portal");
  }
}

// =========================
// SETUP
// =========================

void setup() {
  Serial.begin(115200);
  delay(300);

  for (int i = 0; i < MAX_SESSIONS; i++) {
    sessions[i].active = false;
    sessions[i].token = "";
    sessions[i].created = 0;
  }

  pinMode(RELAY_SPEED1, OUTPUT);
  pinMode(RELAY_SPEED2, OUTPUT);
  pinMode(RELAY_SPEED3, OUTPUT);
  allRelaysOff();

  pinMode(POT_PIN, INPUT);
  analogReadResolution(12);

  loadConfig();

  // Start at the pot position
  potZone = zoneFromValue(readPotAveraged(), -1);
  setFan(potZone);
  if (potZone > 0) lastOnSpeed = potZone;

  lastWifiOk = millis();
  startWiFi();
}

// =========================
// LOOP
// =========================

void loop() {
  if (servicesStarted) {
    server.handleClient();
    if (sinricConfigured) SinricPro.handle();
  }

  // Potentiometer (every 200 ms)
  static unsigned long lastPotRead = 0;
  if (millis() - lastPotRead >= 200) {
    lastPotRead = millis();
    readPotentiometer();
  }

  // Timer (rollover-safe)
  if (timerActive && (long)(millis() - fanTimerEnd) >= 0) {
    Serial.println("Fan timer expired");
    applySpeed(0);
  }

  // Tell the cloud about changes made locally
  sinricReportIfNeeded();

  // Wi-Fi watchdog: restart if offline for 10 minutes (reopens setup portal if needed)
  if (WiFi.status() == WL_CONNECTED) {
    lastWifiOk = millis();
  } else if (millis() - lastWifiOk > 600000UL) {
    Serial.println("Wi-Fi lost for 10 min - restarting");
    ESP.restart();
  }
}
