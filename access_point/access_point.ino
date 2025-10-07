#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define RESET_BUTTON_PIN 0  // Botón conectado al pin GPIO0 (ajústalo si usas otro)

// --- Prototipos ---
void startAccessPoint();
void handleRoot();
void handleSave();
void handleReset();
void loadCredentials();
void saveCredentials(const String& ssid, const String& password);
void clearCredentials();
bool connectToWiFi();

WebServer server(80);
String ssid = "";
String password = "";

// ===================================================
// SETUP PRINCIPAL
// ===================================================
void setup() {
  Serial.begin(115200);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // Inicializar SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error montando SPIFFS");
    return;
  }

  // Si el botón está presionado al inicio → borrar credenciales
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("Botón de reset presionado. Borrando credenciales...");
    clearCredentials();
    delay(1000);
  }

  // Intentar cargar credenciales guardadas
  loadCredentials();

  // Si no hay credenciales o conexión fallida, iniciar AP
  if (ssid == "" || !connectToWiFi()) {
    startAccessPoint();
  }
}

// ===================================================
// LOOP PRINCIPAL
// ===================================================
void loop() {
  server.handleClient();
}

// ===================================================
// MODO AP Y PORTAL DE CONFIGURACIÓN
// ===================================================
void startAccessPoint() {
  Serial.println("Iniciando modo Access Point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_Config", "12345678");
  Serial.println("Red WiFi creada: ESP32_Config");
  Serial.print("IP del AP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/reset", handleReset);
  server.begin();
}

// Página HTML de configuración
void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Configuración WiFi</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        body { font-family: Arial; text-align: center; margin-top: 40px; }
        input { padding: 8px; width: 80%; margin: 6px; }
        button { padding: 10px 20px; margin: 10px; }
      </style>
    </head>
    <body>
      <h2>Configuración de WiFi</h2>
      <form action="/save" method="post">
        <input type="text" name="ssid" placeholder="SSID" required><br>
        <input type="password" name="password" placeholder="Contraseña" required><br>
        <button type="submit">Guardar</button>
      </form>
      <hr>
      <form action="/reset" method="get">
        <button style="background-color:#ff4d4d; color:white;">🗑️ Borrar credenciales</button>
      </form>
    </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// Guardar credenciales y reiniciar
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    ssid = server.arg("ssid");
    password = server.arg("password");
    saveCredentials(ssid, password);
    server.send(200, "text/html", "<h3>Credenciales guardadas. Reiniciando...</h3>");
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Faltan parámetros SSID o password");
  }
}

// Borrar credenciales desde la web
void handleReset() {
  clearCredentials();
  server.send(200, "text/html", "<h3>Credenciales borradas. Reiniciando...</h3>");
  delay(2000);
  ESP.restart();
}

// ===================================================
// MANEJO DE CREDENCIALES EN SPIFFS
// ===================================================
void loadCredentials() {
  File file = SPIFFS.open("/wifi.json", "r");
  if (!file) {
    Serial.println("No se encontraron credenciales guardadas");
    return;
  }

  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("Error leyendo JSON");
    return;
  }

  ssid = doc["ssid"].as<String>();
  password = doc["password"].as<String>();
  Serial.println("Credenciales cargadas:");
  Serial.println("SSID: " + ssid);
  file.close();
}

void saveCredentials(const String& ssid, const String& password) {
  StaticJsonDocument<128> doc;
  doc["ssid"] = ssid;
  doc["password"] = password;

  File file = SPIFFS.open("/wifi.json", "w");
  if (!file) {
    Serial.println("Error guardando credenciales");
    return;
  }

  serializeJson(doc, file);
  file.close();
  Serial.println("Credenciales guardadas correctamente");
}

void clearCredentials() {
  SPIFFS.remove("/wifi.json");
  Serial.println("Credenciales eliminadas del sistema de archivos");
}

// ===================================================
// CONEXIÓN AUTOMÁTICA A RED WIFI
// ===================================================
bool connectToWiFi() {
  if (ssid == "" || password == "") return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Conectado a WiFi");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\n❌ Falló la conexión a WiFi");
    return false;
  }
}
