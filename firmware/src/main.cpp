/*
 * Canadiens Goal Light — ESP32-C3 Super Mini
 * LEDs : WS2812B sur GPIO4
 *
 * Librairies requises (Arduino IDE Library Manager) :
 *   - Adafruit NeoPixel        (Adafruit)
 *   - ArduinoJson              (Benoit Blanchon) v7.x
 *   - WiFiManager              (tzapu) v2.x
 *   - ElegantOTA               (ayushsharma82) v3.x
 *
 * Board : "ESP32C3 Dev Module" dans esp32 by Espressif Systems
 * Reglages board importants :
 *   - USB CDC On Boot : Enabled  (pour Serial via USB)
 *   - Flash Size      : 4MB
 *   - CPU Frequency   : 160MHz
 *
 * Structure du projet (dossier canadiens_goal_light_c3/) :
 *   canadiens_goal_light_c3.ino   <- ce fichier
 *   html_page.h                   <- Web UI HTML
 *
 * Reset WiFi : maintiens GPIO9 (bouton BOOT) 3 sec apres le boot
 *   -> LEDs orange 3x -> efface credentials -> redémarre en hotspot
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ElegantOTA.h>
#include <time.h>
#include "html_page.h"

// ─── CONFIG ────────────────────────────────────────────────────────────────
#define LED_PIN        4
#define LED_COUNT_MAX  300     // Maximum absolu supporté
#define LED_COUNT_DEF  28      // Valeur par défaut
#define POLL_INTERVAL  30000   // ms entre chaque poll NHL
#define GOAL_DURATION  45000   // ms de flash rouge au but
#define RESET_PIN      9       // GPIO9 = bouton BOOT du C3 Super Mini

const int  HABS_TEAM_ID = 8;
const char AP_NAME[]    = "CanadiensGoalLight";
// ───────────────────────────────────────────────────────────────────────────

// ─── FORWARD DECLARATIONS ──────────────────────────────────────────────────
void setAll(uint8_t r, uint8_t g, uint8_t b);
void blinkAll(uint8_t r, uint8_t g, uint8_t b, int times, int delayMs);
void applyIdleColor();
void handleRoot();
void handleState();
void handleSet();
void handleSetLeds();
void handleSetBrightness();
void handleLogs();
void handleReboot();
void onApStarted(WiFiManager* wm);
void checkScore();
String getTodayGameId();
void fetchGameScore(String gameId);
// ───────────────────────────────────────────────────────────────────────────

Adafruit_NeoPixel strip(LED_COUNT_MAX, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer         server(80);
DNSServer         dnsServer;
WiFiManager       wifiManager;
Preferences       prefs;

// ─── ETAT GLOBAL ───────────────────────────────────────────────────────────
bool lightOn    = true;
bool colorRed   = true;
bool colorWhite = false;
bool colorBlue  = false;
bool saveColors = false;
bool offSeason  = false;  // mode hors-saison — désactive le poll NHL
int  ledCount   = LED_COUNT_DEF;
int  brightness = 200;

int           lastScore      = -1;
bool          gameActive     = false;
bool          goalFlashing   = false;
unsigned long goalStartTime  = 0;
unsigned long lastPoll       = 0;
unsigned long resetHoldStart = 0;
bool          resetHolding   = false;

// ─── BUFFER DE LOGS ────────────────────────────────────────────────────────
#define LOG_MAX_LINES  60
#define LOG_LINE_LEN   100
char     logBuffer[LOG_MAX_LINES][LOG_LINE_LEN];
int      logHead  = 0;   // prochain index à écrire (circulaire)
int      logCount = 0;   // nb de lignes remplies

void addLog(const char* fmt, ...) {
  char tmp[LOG_LINE_LEN];
  // Horodatage
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  char ts[10];
  strftime(ts, sizeof(ts), "%H:%M:%S", &ti);

  // Message formaté
  char msg[LOG_LINE_LEN - 12];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  snprintf(logBuffer[logHead], LOG_LINE_LEN, "[%s] %s", ts, msg);
  Serial.println(logBuffer[logHead]);

  logHead = (logHead + 1) % LOG_MAX_LINES;
  if (logCount < LOG_MAX_LINES) logCount++;
}

// ─── HELPERS LED ───────────────────────────────────────────────────────────
void setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < ledCount; i++)
    strip.setPixelColor(i, strip.Color(r, g, b));
  // Éteindre les LEDs au-delà du count actif (si on a réduit)
  for (int i = ledCount; i < LED_COUNT_MAX; i++)
    strip.setPixelColor(i, 0);
  strip.show();
}

void blinkAll(uint8_t r, uint8_t g, uint8_t b, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    setAll(r, g, b); delay(delayMs);
    setAll(0, 0, 0); delay(delayMs);
  }
}

// Calcule et applique la couleur idle selon les toggles actifs
void applyIdleColor() {
  if (!lightOn) { setAll(0, 0, 0); return; }
  int r = 0, g = 0, b = 0;
  if (colorRed)   r = 255;
  if (colorWhite) { r = min(255, r + 255); g = 255; b = 255; }
  if (colorBlue)  b = min(255, b + 255);
  // Aucune couleur cochee -> blanc par defaut
  if (!colorRed && !colorWhite && !colorBlue) { r = 255; g = 255; b = 255; }
  setAll((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// ─── ROUTES WEB ────────────────────────────────────────────────────────────
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleState() {
  JsonDocument doc;
  doc["on"]         = lightOn;
  doc["red"]        = colorRed;
  doc["white"]      = colorWhite;
  doc["blue"]       = colorBlue;
  doc["gameActive"] = gameActive;
  doc["flashing"]   = goalFlashing;
  doc["score"]      = lastScore;
  doc["ip"]         = WiFi.localIP().toString();
  doc["ledCount"]   = ledCount;
  doc["saveColors"] = saveColors;
  doc["brightness"] = brightness;
  doc["offSeason"]  = offSeason;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void _saveColorPrefs() {
  prefs.begin("goallight", false);
  prefs.putBool("colorRed",   colorRed);
  prefs.putBool("colorWhite", colorWhite);
  prefs.putBool("colorBlue",  colorBlue);
  prefs.putBool("lightOn",    lightOn);
  prefs.end();
}

void handleSet() {
  if (server.hasArg("power")) {
    lightOn = (server.arg("power") == "1");
    if (!goalFlashing) applyIdleColor();
    if (saveColors) _saveColorPrefs();
  }
  if (server.hasArg("color") && server.hasArg("val")) {
    String c   = server.arg("color");
    bool   val = (server.arg("val") == "1");
    if (c == "rouge") colorRed   = val;
    if (c == "blanc") colorWhite = val;
    if (c == "bleu")  colorBlue  = val;
    if (!goalFlashing) applyIdleColor();
    if (saveColors) _saveColorPrefs();
  }
  if (server.hasArg("savecolors")) {
    saveColors = (server.arg("savecolors") == "1");
    prefs.begin("goallight", false);
    prefs.putBool("saveColors", saveColors);
    prefs.end();
    if (saveColors) _saveColorPrefs();
    addLog("Sauvegarde couleurs: %s", saveColors ? "ON" : "OFF");
  }
  if (server.hasArg("offseason")) {
    offSeason = (server.arg("offseason") == "1");
    prefs.begin("goallight", false);
    prefs.putBool("offSeason", offSeason);
    prefs.end();
    if (offSeason) { lastScore = -1; gameActive = false; }
    addLog("Mode hors-saison: %s", offSeason ? "ON" : "OFF");
  }
  server.send(200, "text/plain", "OK");
}

void handleSetLeds() {
  if (server.hasArg("count")) {
    int val = server.arg("count").toInt();
    if (val >= 1 && val <= LED_COUNT_MAX) {
      ledCount = val;
      prefs.begin("goallight", false);
      prefs.putInt("ledCount", ledCount);
      prefs.end();
      strip.updateLength(ledCount);
      if (!goalFlashing) applyIdleColor();
      addLog("LED count mis a jour: %d", ledCount);
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Valeur invalide (1-300)");
    }
  } else {
    server.send(400, "text/plain", "Parametre manquant");
  }
}

void handleSetBrightness() {
  if (server.hasArg("val")) {
    int val = server.arg("val").toInt();
    if (val >= 0 && val <= 255) {
      brightness = val;
      strip.setBrightness(brightness);
      if (!goalFlashing) applyIdleColor();
      prefs.begin("goallight", false);
      prefs.putInt("brightness", brightness);
      prefs.end();
      addLog("Luminosite: %d%%", (brightness * 100) / 255);
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Valeur invalide (0-255)");
    }
  } else {
    server.send(400, "text/plain", "Parametre manquant");
  }
}

void handleLogs() {
  // Retourne les logs en ordre chronologique (du plus vieux au plus récent)
  String out = "";
  int total = min(logCount, LOG_MAX_LINES);
  int start = (logCount < LOG_MAX_LINES) ? 0 : logHead;
  for (int i = 0; i < total; i++) {
    int idx = (start + i) % LOG_MAX_LINES;
    out += logBuffer[idx];
    out += "\n";
  }
  server.send(200, "text/plain", out);
}

void handleReboot() {
  server.send(200, "text/plain", "Reboot en cours...");
  addLog("Reboot demande via Web UI");
  delay(500);
  ESP.restart();
}

// ─── CALLBACK WiFiManager ──────────────────────────────────────────────────
void onApStarted(WiFiManager* wm) {
  addLog("Mode config AP — SSID: CanadiensGoalLight");
  blinkAll(0, 0, 255, 5, 400);
}

// ─── SETUP ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(RESET_PIN, INPUT_PULLUP);

  strip.begin();

  // Charger ledCount, brightness et couleurs depuis NVS
  prefs.begin("goallight", true);
  ledCount   = prefs.getInt("ledCount",    LED_COUNT_DEF);
  brightness = prefs.getInt("brightness",  200);
  offSeason  = prefs.getBool("offSeason",  false);
  saveColors = prefs.getBool("saveColors", false);
  if (saveColors) {
    colorRed   = prefs.getBool("colorRed",   true);
    colorWhite = prefs.getBool("colorWhite", false);
    colorBlue  = prefs.getBool("colorBlue",  false);
    lightOn    = prefs.getBool("lightOn",    true);
  }
  prefs.end();
  strip.updateLength(ledCount);
  strip.setBrightness(brightness);
  setAll(0, 0, 0);

  // Reset WiFi si bouton BOOT maintenu au demarrage
  unsigned long holdStart = millis();
  while (digitalRead(RESET_PIN) == LOW) {
    if (millis() - holdStart > 3000) {
      addLog("Reset WiFi demande...");
      blinkAll(255, 80, 0, 3, 150);
      wifiManager.resetSettings();
      delay(300);
      ESP.restart();
    }
  }

  // WiFiManager
  wifiManager.setAPCallback(onApStarted);
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(20);

  if (!wifiManager.autoConnect(AP_NAME)) {
    addLog("Timeout WiFiManager — reboot...");
    blinkAll(255, 0, 0, 5, 100);
    delay(500);
    ESP.restart();
  }

  addLog("WiFi connecte! IP: %s", WiFi.localIP().toString().c_str());
  blinkAll(255, 255, 255, 2, 200);

  // NTP — HNE (UTC-5) / HAE (UTC-4) passage auto 2e dim. mars / 1er dim. nov.
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
  addLog("Sync NTP...");
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 100000 && attempts < 20) {
    delay(500); now = time(nullptr); attempts++;
  }
  addLog("NTP OK — LED count: %d", ledCount);

  // Web server
  server.on("/",          handleRoot);
  server.on("/state",     handleState);
  server.on("/set",       handleSet);
  server.on("/setleds",   handleSetLeds);
  server.on("/brightness",handleSetBrightness);
  server.on("/logs",      handleLogs);
  server.on("/reboot",    handleReboot);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  dnsServer.start(53, "*", WiFi.localIP());
  ElegantOTA.begin(&server);
  server.begin();
  addLog("Web UI pret: http://%s", WiFi.localIP().toString().c_str());
  addLog("OTA pret: http://%s/update", WiFi.localIP().toString().c_str());

  applyIdleColor();
}

// ─── LOOP ──────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();
  dnsServer.processNextRequest();
  ElegantOTA.loop();

  // Reset WiFi via bouton BOOT (maintien 3 sec)
  if (digitalRead(RESET_PIN) == LOW) {
    if (!resetHolding) { resetHolding = true; resetHoldStart = millis(); }
    if (millis() - resetHoldStart > 3000) {
      blinkAll(255, 80, 0, 3, 150);
      wifiManager.resetSettings();
      delay(300);
      ESP.restart();
    }
  } else {
    resetHolding = false;
  }

  // Flash but en cours
  if (goalFlashing) {
    unsigned long elapsed = millis() - goalStartTime;
    if (elapsed >= GOAL_DURATION) {
      goalFlashing = false;
      applyIdleColor();
    } else {
      // Strobe rouge toutes les 150ms
      setAll((elapsed / 150) % 2 == 0 ? 255 : 0, 0, 0);
      delay(50);
    }
    return;
  }

  // Poll NHL toutes les POLL_INTERVAL ms (désactivé en mode hors-saison)
  if (!offSeason && (millis() - lastPoll >= POLL_INTERVAL || lastPoll == 0)) {
    lastPoll = millis();
    checkScore();
  }

  delay(50);
}

// ─── CHECK SCORE ───────────────────────────────────────────────────────────
void checkScore() {
  if (WiFi.status() != WL_CONNECTED) {
    addLog("WiFi perdu — reconnexion...");
    WiFi.reconnect();
    delay(5000);
    if (WiFi.status() != WL_CONNECTED) ESP.restart();
    return;
  }
  String gameId = getTodayGameId();
  if (gameId == "") {
    addLog("Pas de game aujourd'hui.");
    lastScore  = -1;
    gameActive = false;
    return;
  }
  fetchGameScore(gameId);
}

// ─── GET TODAY'S GAME ID ───────────────────────────────────────────────────
String getTodayGameId() {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &ti);

  String url = String("https://api-web.nhle.com/v1/schedule/") + dateStr;
  addLog("Poll schedule: %s", dateStr);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  if (code != 200) {
    addLog("HTTP schedule erreur: %d", code);
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    addLog("JSON erreur schedule");
    return "";
  }

  for (JsonObject day : doc["gameWeek"].as<JsonArray>()) {
    for (JsonObject game : day["games"].as<JsonArray>()) {
      int homeId = game["homeTeam"]["id"].as<int>();
      int awayId = game["awayTeam"]["id"].as<int>();
      if (homeId == HABS_TEAM_ID || awayId == HABS_TEAM_ID) {
        String id    = game["id"].as<String>();
        String state = game["gameState"].as<String>();
        addLog("Game trouvee: %s  etat: %s", id.c_str(), state.c_str());
        if (state == "LIVE" || state == "CRIT" || state == "PRE" || state == "FUT") {
          gameActive = (state == "LIVE" || state == "CRIT");
          return id;
        }
      }
    }
  }
  return "";
}

// ─── FETCH GAME SCORE ──────────────────────────────────────────────────────
void fetchGameScore(String gameId) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api-web.nhle.com/v1/gamecenter/" + gameId + "/landing";
  http.begin(client, url);
  int code = http.GET();
  if (code != 200) {
    addLog("HTTP score erreur: %d", code);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    addLog("JSON erreur score");
    return;
  }

  int homeId    = doc["homeTeam"]["id"].as<int>();
  int habsScore = (homeId == HABS_TEAM_ID)
                    ? doc["homeTeam"]["score"].as<int>()
                    : doc["awayTeam"]["score"].as<int>();

  String gameState = doc["gameState"].as<String>();
  gameActive = (gameState == "LIVE" || gameState == "CRIT");

  addLog("Score Canadiens: %d  (%s)", habsScore, gameState.c_str());

  if (lastScore == -1) {
    lastScore = habsScore;
    addLog("Score initialise a %d.", habsScore);
    return;
  }

  if (gameActive && habsScore > lastScore) {
    addLog("*** BUT! Score: %d -> %d ***", lastScore, habsScore);
    lastScore     = habsScore;
    goalFlashing  = true;
    goalStartTime = millis();
  } else {
    lastScore = habsScore;
  }
}
