/*
  wifi_provisioning.ino (versión con opción web y serial para borrar credenciales)
  - Timeout de 15s en conexión STA
  - Permite borrar credenciales desde Serial ('r' o 'R') y desde web (/api/reset)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#define AP_SSID      "ESP32-Provision"
#define AP_PASSWORD  "provision123"
#define AP_IP_FIRST  192
#define AP_IP_SECOND 168
#define AP_IP_THIRD  4
#define AP_IP_FOURTH 1

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

IPAddress apIP(AP_IP_FIRST, AP_IP_SECOND, AP_IP_THIRD, AP_IP_FOURTH);

bool runningInAP = false;
unsigned long lastConnectAttempt = 0;

void startAP();
void startSTAWeb();
void handleRoot();
void handleSave();
void handleApiCredentials();
void handleApiStatus();
void handleApiReset();
void handleScan();
void handleNotFound();
void saveCredentials(const String &ssid, const String &pass);
void clearCredentials();
bool hasSavedCredentials();
String getSavedSSID();
String getSavedPASS();
void tryConnectSaved();

void setup() {
  Serial.begin(115200);
  delay(500);

  prefs.begin("wifi", false);

  Serial.println();
  Serial.println("=== ESP32 WiFi Provisioning ===");
  Serial.println("Comandos disponibles:");
  Serial.println("  r  →  borrar credenciales y reiniciar");
  Serial.println();

  if (hasSavedCredentials()) {
    Serial.println("Credenciales guardadas encontradas. Intentando conectar...");
    tryConnectSaved();
  } else {
    Serial.println("No hay credenciales guardadas.");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Arrancando en modo AP (provisioning)...");
    startAP();
  }
}

void loop() {
  // --- Leer comandos por Serial ---
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      Serial.println("⚠️ Borrando credenciales guardadas...");
      clearCredentials();
      delay(800);
      ESP.restart();
    }
  }

  if (runningInAP) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else {
    server.handleClient();
    if (WiFi.status() != WL_CONNECTED) {
      unsigned long now = millis();
      if (now - lastConnectAttempt > 10000) {
        Serial.println("Conexión perdida. Reintentando...");
        lastConnectAttempt = now;
        tryConnectSaved();
      }
    }
    delay(10);
  }
}

/* ---------- Start AP ---------- */
void startAP() {
  runningInAP = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  dnsServer.start(53, "*", apIP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/api/credentials", HTTP_POST, handleApiCredentials);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.onNotFound(handleNotFound);

  server.begin();

  Serial.print("AP iniciado. SSID: ");
  Serial.print(AP_SSID);
  Serial.print("  IP: ");
  Serial.println(apIP.toString());
}

/* ---------- Start STA Web ---------- */
void startSTAWeb() {
  runningInAP = false;
  WiFi.mode(WIFI_STA);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Servidor HTTP iniciado en modo STA.");
}

/* ---------- Página principal ---------- */
void handleRoot() {
  String page = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1' />";
  page += "<title>ESP32 WiFi Provision</title></head><body style='font-family:sans-serif;text-align:center'>";

  if (runningInAP) {
    page += "<h2>ESP32 - Provisioning WiFi</h2>";
    page += "<form action='/save' method='POST'>";
    page += "SSID: <input name='ssid' id='ssid' required><br>";
    page += "Password: <input name='password' id='password' type='password'><br><br>";
    page += "<button type='submit'>Guardar y conectar</button>";
    page += "</form>";
    page += "<br><button onclick='scan()'>Listar redes</button>";
    page += "<ul id='networks'></ul>";
    page += "<hr><button onclick='resetCreds()' style='background:red;color:white'>🧹 Borrar credenciales</button>";
    page += "<script>";
    page += "function scan(){fetch('/scan').then(r=>r.json()).then(rs=>{let ul=document.getElementById('networks');ul.innerHTML='';rs.forEach(n=>{let li=document.createElement('li');let b=document.createElement('button');b.textContent='Usar';b.onclick=()=>{document.getElementById('ssid').value=n.ssid};li.appendChild(document.createTextNode(n.ssid+' (RSSI:'+n.rssi+') '));li.appendChild(b);ul.appendChild(li);});});}";
    page += "function resetCreds(){fetch('/api/reset',{method:'POST'}).then(()=>{alert('Credenciales borradas. Reiniciando...');});}";
    page += "</script>";
  } else {
    page += "<h2>ESP32 - Estado</h2>";
    page += "<p>Conectado a SSID: " + WiFi.SSID() + "</p>";
    page += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    page += "<form method='POST' action='/api/reset'><button type='submit' style='background:red;color:white'>🧹 Borrar credenciales</button></form>";
  }

  page += "</body></html>";
  server.send(200, "text/html", page);
}

/* ---------- Handlers ---------- */
void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"ssid vacío\"}");
    return;
  }
  saveCredentials(ssid, password);
  server.send(200, "application/json", "{\"result\":\"ok\",\"restarting\":true}");
  delay(800);
  ESP.restart();
}

void handleApiCredentials() {
  String body = server.arg("plain");
  String ssid = "", password = "";
  if (body.length() > 0) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (!err) {
      ssid = doc["ssid"] | "";
      password = doc["password"] | "";
    } else {
      server.send(400, "application/json", "{\"error\":\"JSON inválido\"}");
      return;
    }
  } else {
    ssid = server.arg("ssid");
    password = server.arg("password");
  }

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"ssid vacío\"}");
    return;
  }
  saveCredentials(ssid, password);
  server.send(200, "application/json", "{\"result\":\"ok\",\"restarting\":true}");
  delay(800);
  ESP.restart();
}

void handleApiStatus() {
  DynamicJsonDocument doc(256);
  doc["connected"] = (WiFi.status() == WL_CONNECTED);
  if (WiFi.status() == WL_CONNECTED) {
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
  } else {
    doc["ap_mode"] = runningInAP;
    doc["ap_ssid"] = AP_SSID;
    doc["ap_ip"] = apIP.toString();
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiReset() {
  clearCredentials();
  server.send(200, "application/json", "{\"result\":\"ok\",\"restarting\":true}");
  delay(800);
  ESP.restart();
}

void handleScan() {
  int16_t n = WiFi.scanNetworks();
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject item = arr.createNestedObject();
    item["ssid"] = WiFi.SSID(i);
    item["rssi"] = WiFi.RSSI(i);
    item["enc"] = WiFi.encryptionType(i);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
  WiFi.scanDelete();
}

void handleNotFound() {
  if (runningInAP) {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

/* ---------- Utilities ---------- */
void saveCredentials(const String &ssid, const String &pass) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  Serial.println("Credenciales guardadas en NVS.");
}

void clearCredentials() {
  prefs.clear();
  Serial.println("✅ Credenciales borradas de NVS.");
}

bool hasSavedCredentials() {
  return prefs.getString("ssid", "").length() > 0;
}

String getSavedSSID() { return prefs.getString("ssid", ""); }
String getSavedPASS() { return prefs.getString("pass", ""); }

void tryConnectSaved() {
  String ssid = getSavedSSID();
  String pass = getSavedPASS();
  if (ssid.length() == 0) return;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("Conectando a ");
  Serial.print(ssid);

  unsigned long start = millis();
  const unsigned long timeout = 15000; // 15s

  while (millis() - start < timeout) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.print("✅ Conectado a ");
      Serial.print(ssid);
      Serial.print(" | IP: ");
      Serial.println(WiFi.localIP());
      startSTAWeb();
      lastConnectAttempt = millis();
      return;
    }
    Serial.print(".");
    delay(500);
  }

  Serial.println("\n❌ No se pudo conectar (timeout). Cambiando a modo AP...");
  startAP();
}
