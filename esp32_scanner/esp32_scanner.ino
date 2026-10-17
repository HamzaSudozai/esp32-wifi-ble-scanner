/*
  ESP32 Wi-Fi + BLE Environment Scanner
  -------------------------------------
  Scans nearby Wi-Fi networks and BLE devices in a background task and
  serves the results as a live dashboard from the ESP32 itself.

  Board settings (Arduino IDE):
    Board:            "ESP32 Dev Module"
    Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"   <-- needed, Wi-Fi + BLE is big
    Upload Speed:     115200 (if uploads are flaky)

  Only scan environments you own or are allowed to observe.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ---------- CONFIG ----------
// Wi-Fi credentials live in secrets.h (not committed to git).
// Copy secrets.h.example to secrets.h and fill in your own values.
#include "secrets.h"

// Fallback hotspot if the board can't join your Wi-Fi (browse to 192.168.4.1)
const char* AP_SSID = "ESP32-Scanner";
const char* AP_PASS = "scanner1234";

const int BLE_SCAN_SECONDS = 5;
// ----------------------------

WebServer server(80);
SemaphoreHandle_t dataMutex;
String wifiJson = "[]";
String bleJson  = "[]";
unsigned long lastScanMs = 0;

// ---------- helpers ----------
String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c < 0x20) { /* drop control characters */ }
    else out += c;
  }
  return out;
}

const char* encName(wifi_auth_mode_t t) {
  switch (t) {
    case WIFI_AUTH_OPEN:            return "Open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    default:                        return "Other";
  }
}

// ---------- Wi-Fi scan ----------
void doWifiScan() {
  int n = WiFi.scanNetworks(false, true);  // blocking, include hidden SSIDs
  String j = "[";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) ssid = "<hidden>";
    if (i) j += ",";
    j += "{\"ssid\":\"" + jsonEscape(ssid) + "\"";
    j += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
    j += ",\"ch\":" + String(WiFi.channel(i));
    j += ",\"rssi\":" + String(WiFi.RSSI(i));
    j += ",\"enc\":\"" + String(encName(WiFi.encryptionType(i))) + "\"}";
  }
  j += "]";
  WiFi.scanDelete();

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  wifiJson = j;
  xSemaphoreGive(dataMutex);
  Serial.printf("[WiFi] %d networks\n", n);
}

// ---------- BLE scan ----------
String bleResultsToJson(BLEScanResults& r) {
  String j = "[";
  int n = r.getCount();
  for (int i = 0; i < n; i++) {
    auto d = r.getDevice(i);

    String mac  = String(d.getAddress().toString().c_str());
    String name = d.haveName() ? String(d.getName().c_str()) : String("");
    String uuid = d.haveServiceUUID() ? String(d.getServiceUUID().toString().c_str()) : String("");

    // First two bytes of manufacturer data = Bluetooth company ID (little endian)
    String company = "";
    if (d.haveManufacturerData()) {
      auto m = d.getManufacturerData();
      if (m.length() >= 2) {
        const char* p = m.c_str();
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X%02X", (uint8_t)p[1], (uint8_t)p[0]);
        company = buf;
      }
    }

    if (i) j += ",";
    j += "{\"mac\":\"" + mac + "\"";
    j += ",\"name\":\"" + jsonEscape(name) + "\"";
    j += ",\"rssi\":" + String(d.getRSSI());
    j += ",\"uuid\":\"" + jsonEscape(uuid) + "\"";
    j += ",\"company\":\"" + company + "\"}";
  }
  j += "]";
  return j;
}

void doBleScan() {
  BLEScan* scan = BLEDevice::getScan();
  String j;
  int n = 0;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  BLEScanResults* res = scan->start(BLE_SCAN_SECONDS, false);
  j = bleResultsToJson(*res);
  n = res->getCount();
#else
  BLEScanResults res = scan->start(BLE_SCAN_SECONDS, false);
  j = bleResultsToJson(res);
  n = res.getCount();
#endif
  scan->clearResults();

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  bleJson = j;
  lastScanMs = millis();
  xSemaphoreGive(dataMutex);
  Serial.printf("[BLE]  %d devices\n", n);
}

// ---------- background task ----------
void scanTask(void*) {
  for (;;) {
    doWifiScan();
    doBleScan();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ---------- web page ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Air Scanner</title>
<style>
  :root { --bg:#0f1419; --card:#1a2129; --text:#e6edf3; --mute:#8b98a5; --line:#2a343e;
          --ok:#3fb950; --warn:#d29922; --bad:#f85149; --acc:#58a6ff; }
  * { box-sizing:border-box; }
  body { margin:0; font-family:system-ui,sans-serif; background:var(--bg); color:var(--text); padding:16px; }
  h1 { font-size:20px; margin:0 0 4px; }
  .sub { color:var(--mute); font-size:13px; margin-bottom:16px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:14px; margin-bottom:16px; }
  h2 { font-size:15px; margin:0 0 10px; }
  .scroll { overflow-x:auto; }
  table { width:100%; border-collapse:collapse; font-size:13px; }
  th, td { text-align:left; padding:6px 8px; border-bottom:1px solid var(--line); white-space:nowrap; }
  th { color:var(--mute); font-weight:600; cursor:pointer; }
  .ok { color:var(--ok); } .warn { color:var(--warn); } .bad { color:var(--bad); font-weight:600; }
  .bar { display:inline-block; height:8px; background:var(--acc); border-radius:4px; vertical-align:middle; margin-right:6px; }
  .stats { display:flex; gap:16px; flex-wrap:wrap; font-size:13px; color:var(--mute); margin-bottom:10px; }
  .stats b { color:var(--text); }
  input { background:var(--bg); color:var(--text); border:1px solid var(--line); border-radius:6px; padding:6px 8px; width:100%; max-width:260px; margin-bottom:10px; }
</style>
</head>
<body>
<h1>ESP32 Air Scanner</h1>
<div class="sub" id="status">Loading...</div>

<div class="card">
  <h2>Wi-Fi networks</h2>
  <div class="stats" id="wstats"></div>
  <div class="scroll"><table id="wt"></table></div>
</div>

<div class="card">
  <h2>BLE devices</h2>
  <input id="filter" placeholder="Filter by name / MAC / company ID">
  <div class="scroll"><table id="bt"></table></div>
</div>

<script>
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const encClass = e => (e==='Open'||e==='WEP') ? 'bad' : (e==='WPA') ? 'warn' : 'ok';
const barW = r => Math.max(4, Math.min(100, 2*(r+100)));   // -100..-50 dBm -> 0..100 px
let data = {wifi:[], ble:[]};

const render = () => {
  const w = [...data.wifi].sort((a,b)=>b.rssi-a.rssi);
  const open = w.filter(n=>n.enc==='Open'||n.enc==='WEP').length;
  const chans = {}; w.forEach(n=>chans[n.ch]=(chans[n.ch]||0)+1);
  const busiest = Object.entries(chans).sort((a,b)=>b[1]-a[1])[0];
  document.getElementById('wstats').innerHTML =
    `<span>Networks: <b>${w.length}</b></span>` +
    `<span>Open/WEP: <b class="${open?'bad':'ok'}">${open}</b></span>` +
    `<span>Busiest channel: <b>${busiest?busiest[0]+' ('+busiest[1]+')':'-'}</b></span>`;
  document.getElementById('wt').innerHTML =
    '<tr><th>SSID</th><th>BSSID</th><th>Ch</th><th>Signal</th><th>Security</th></tr>' +
    w.map(n=>`<tr><td>${esc(n.ssid)}</td><td>${esc(n.bssid)}</td><td>${n.ch}</td>
      <td><span class="bar" style="width:${barW(n.rssi)}px"></span>${n.rssi} dBm</td>
      <td class="${encClass(n.enc)}">${esc(n.enc)}</td></tr>`).join('');

  const q = document.getElementById('filter').value.toLowerCase();
  const b = [...data.ble].sort((a,b)=>b.rssi-a.rssi)
    .filter(d=>!q || (d.name+d.mac+d.company).toLowerCase().includes(q));
  document.getElementById('bt').innerHTML =
    '<tr><th>Name</th><th>MAC</th><th>Signal</th><th>Company ID</th><th>Service UUID</th></tr>' +
    b.map(d=>`<tr><td>${esc(d.name)||'<i>unnamed</i>'}</td><td>${esc(d.mac)}</td>
      <td><span class="bar" style="width:${barW(d.rssi)}px"></span>${d.rssi} dBm</td>
      <td>${esc(d.company)}</td><td>${esc(d.uuid)}</td></tr>`).join('');
}

const load = async () => {
  try {
    const r = await fetch('/api/data');
    data = await r.json();
    document.getElementById('status').textContent =
      `Last scan ${Math.round(data.age/1000)}s ago | ${data.wifi.length} Wi-Fi, ${data.ble.length} BLE`;
    render();
  } catch (e) {
    document.getElementById('status').textContent = 'Connection lost, retrying...';
  }
}
document.getElementById('filter').addEventListener('input', render);
setInterval(load, 4000); load();
</script>
</body>
</html>
)rawliteral";

// ---------- HTTP handlers ----------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  String out = "{\"wifi\":" + wifiJson + ",\"ble\":" + bleJson +
               ",\"age\":" + String(millis() - lastScanMs) + "}";
  xSemaphoreGive(dataMutex);
  server.send(200, "application/json", out);
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  dataMutex = xSemaphoreCreateMutex();

  // Join home Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Open this in your browser: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Could not join Wi-Fi, starting fallback hotspot.");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("Connect to '");
    Serial.print(AP_SSID);
    Serial.print("' then open http://");
    Serial.println(WiFi.softAPIP());
  }

  // BLE setup
  BLEDevice::init("");
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);   // active scan also requests scan-response data (names)
  scan->setInterval(100);
  scan->setWindow(99);

  // Web server
  server.on("/", handleRoot);
  server.on("/api/data", handleData);
  server.begin();

  // Scanning runs on core 0 so the web server (core 1) stays responsive
  xTaskCreatePinnedToCore(scanTask, "scanTask", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  server.handleClient();
  delay(2);
}
