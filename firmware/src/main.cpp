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
 * Structure du projet (repo github.com/julz24/goal_light) :
 *   firmware/src/main.cpp     <- ce fichier
 *   firmware/src/html_page.h  <- Web UI HTML
 *   firmware/src/version.h    <- généré par GitHub Actions (pas dans le repo)
 *
 * Mise à jour par GitHub :
 *   1. git tag v1.2.0 && git push --tags
 *   2. GitHub Actions compile et publie canadiens_goal_light_c3.bin + version.json
 *   3. Le C3 vérifie 1x/jour (ou bouton "Vérifier") et s'installe
 *      (bouton "Installer", ou automatiquement si l'option est activée)
 *   Build local (Arduino IDE / PlatformIO) = version "dev" : jamais de MAJ auto.
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
#include <HTTPUpdate.h>
#include <time.h>
#include "html_page.h"

// Version : injectée par GitHub Actions dans version.h (à partir du tag v*)
#if __has_include("version.h")
  #include "version.h"
#endif
#ifndef FW_VERSION
  #define FW_VERSION "dev"
#endif

// Délai de lecture du flux TLS : 15 s.
// Core ESP32 3.x (Arduino IDE) = millisecondes, core 2.x (PlatformIO / GitHub Actions) = secondes
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define READ_TIMEOUT 15000
#else
  #define READ_TIMEOUT 15
#endif

// ─── CONFIG ────────────────────────────────────────────────────────────────
#define LED_PIN        4
#define LED_COUNT_MAX  300     // Maximum absolu supporté
#define LED_COUNT_DEF  28      // Valeur par défaut
#define POLL_IDLE      3600000UL  // 60 min — aucun match aujourd'hui / match terminé
#define POLL_SCHEDULED  300000UL  // 5 min  — match à l'horaire, pas commencé (FUT)
#define POLL_LIVE        15000UL  // 15 sec — avant-match / en cours (PRE, LIVE, CRIT)
#define POLL_RETRY       30000UL  // 30 sec — après une erreur réseau/JSON
#define GOAL_DURATION  45000   // ms de flash rouge au but
#define RESET_PIN      9       // GPIO9 = bouton BOOT du C3 Super Mini

// Mise à jour par GitHub Releases
#define GH_REPO          "julz24/goal_light"
#define FW_BIN_URL       "https://github.com/" GH_REPO "/releases/latest/download/canadiens_goal_light_c3.bin"
#define FW_VER_URL       "https://github.com/" GH_REPO "/releases/latest/download/version.json"
#define FW_CHECK_EVERY   86400000UL  // vérification 1x par 24 h
#define FW_CHECK_RETRY    3600000UL  // 1 h après un échec de vérification
#define FW_FIRST_CHECK      60000UL  // 1re vérification 1 min après le boot

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
void handleFwCheck();
void handleFwUpdate();
bool checkFirmware();
void doFirmwareUpdate();
void onApStarted(WiFiManager* wm);
void checkScore();
void fetchGameScore(const String& gameId);
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
unsigned long pollInterval   = POLL_LIVE;  // ajusté dynamiquement selon l'horaire

void addLog(const char* fmt, ...);  // défini plus bas

// Change l'intervalle de poll et logue seulement s'il change
void setPollInterval(unsigned long ms) {
  if (ms != pollInterval) {
    pollInterval = ms;
    if (ms >= 60000UL) addLog("Prochain poll dans %lu min", ms / 60000UL);
    else               addLog("Prochain poll dans %lu sec", ms / 1000UL);
  }
}
unsigned long resetHoldStart = 0;
bool          resetHolding   = false;

// ─── ÉTAT MISE À JOUR ──────────────────────────────────────────────────────
String        fwLatest          = "";     // dernière version publiée sur GitHub
bool          fwUpdateAvailable = false;
bool          fwUpdateRequested = false;  // demandé par le Web UI / l'app
bool          fwUpdating        = false;
bool          autoUpdate        = false;  // NVS — installer automatiquement
unsigned long fwNextCheck       = FW_FIRST_CHECK;

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
  doc["firmware"]   = FW_VERSION;
  doc["fwLatest"]   = fwLatest;
  doc["fwUpdate"]   = fwUpdateAvailable;
  doc["fwUpdating"] = fwUpdating;
  doc["autoUpdate"] = autoUpdate;
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
    else           { lastPoll = 0; }  // reprise : poll immédiat au prochain loop
    addLog("Mode hors-saison: %s", offSeason ? "ON" : "OFF");
  }
  if (server.hasArg("autoupdate")) {
    autoUpdate = (server.arg("autoupdate") == "1");
    prefs.begin("goallight", false);
    prefs.putBool("autoUpdate", autoUpdate);
    prefs.end();
    addLog("MAJ automatique: %s", autoUpdate ? "ON" : "OFF");
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

// ─── MISE À JOUR PAR GITHUB ────────────────────────────────────────────────
// Compare deux versions "1.2.3" (un "v" devant est ignoré, "dev" = 0.0.0)
bool versionNewer(const String& latest, const char* current) {
  int a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
  const char* a = latest.c_str();
  if (*a == 'v') a++;
  if (*current == 'v') current++;
  sscanf(a,       "%d.%d.%d", &a1, &a2, &a3);
  sscanf(current, "%d.%d.%d", &b1, &b2, &b3);
  if (a1 != b1) return a1 > b1;
  if (a2 != b2) return a2 > b2;
  return a3 > b3;
}

// Lit version.json de la dernière release (quelques octets)
bool checkFirmware() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub redirige vers son CDN
  http.setTimeout(15000);
  http.begin(client, FW_VER_URL);
  int code = http.GET();
  if (code != 200) {
    addLog("MAJ: verification echouee (HTTP %d)", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    addLog("MAJ: version.json invalide");
    return false;
  }
  String v = doc["version"] | "";
  if (v.startsWith("v")) v = v.substring(1);
  if (v == "") {
    addLog("MAJ: version.json sans version");
    return false;
  }
  fwLatest          = v;
  fwUpdateAvailable = versionNewer(v, FW_VERSION);
  addLog("MAJ: installee %s, publiee %s%s", FW_VERSION, v.c_str(),
         fwUpdateAvailable ? " -> disponible" : " (a jour)");
  return true;
}

// Télécharge et flashe le .bin de la dernière release, puis redémarre
void doFirmwareUpdate() {
  fwUpdating = true;
  addLog("MAJ: telechargement de la version %s...", fwLatest.c_str());
  setAll(0, 0, 0);

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);           // on logue avant de redémarrer
  httpUpdate.onProgress([](int cur, int total) {
    // Barre de progression bleue sur les LEDs
    static int lastLit = -1;
    if (total <= 0) return;
    int lit = (int)((long)ledCount * cur / total);
    if (lit == lastLit) return;
    lastLit = lit;
    for (int i = 0; i < ledCount; i++)
      strip.setPixelColor(i, i < lit ? strip.Color(0, 0, 255) : 0);
    strip.show();
  });

  t_httpUpdate_return r = httpUpdate.update(client, FW_BIN_URL);

  if (r == HTTP_UPDATE_OK) {
    addLog("MAJ: OK — redemarrage");
    blinkAll(0, 255, 0, 3, 200);              // vert 3x = succès
    delay(300);
    ESP.restart();
  }
  addLog("MAJ: echec (%d) %s", httpUpdate.getLastError(),
         httpUpdate.getLastErrorString().c_str());
  blinkAll(255, 0, 0, 3, 200);                // rouge 3x = échec
  fwUpdating = false;
  applyIdleColor();                           // l'ancien firmware continue
}

void handleFwCheck() {
  bool ok = checkFirmware();
  fwNextCheck = millis() + FW_CHECK_EVERY;
  JsonDocument doc;
  doc["ok"]        = ok;
  doc["current"]   = FW_VERSION;
  doc["latest"]    = fwLatest;
  doc["available"] = fwUpdateAvailable;
  String out;
  serializeJson(doc, out);
  server.send(ok ? 200 : 502, "application/json", out);
}

void handleFwUpdate() {
  if (!fwUpdateAvailable) {
    server.send(409, "text/plain", "Deja a jour (verifier d'abord)");
    return;
  }
  fwUpdateRequested = true;                    // exécuté dans loop(), pas ici
  server.send(200, "text/plain", "Mise a jour lancee");
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
  autoUpdate = prefs.getBool("autoUpdate", false);
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
  server.on("/fwcheck",   handleFwCheck);
  server.on("/fwupdate",  handleFwUpdate);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  dnsServer.start(53, "*", WiFi.localIP());
  ElegantOTA.begin(&server);
  server.begin();
  addLog("Web UI pret: http://%s", WiFi.localIP().toString().c_str());
  addLog("OTA pret: http://%s/update", WiFi.localIP().toString().c_str());
  addLog("Firmware: %s", FW_VERSION);

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

  // Mise à jour demandée par le Web UI / l'app
  if (fwUpdateRequested) {
    fwUpdateRequested = false;
    doFirmwareUpdate();          // redémarre si succès
    return;
  }

  // Vérification GitHub 1x/jour — jamais pendant un match
  bool gameBusy = !offSeason && (gameActive || pollInterval == POLL_LIVE);
  if (!gameBusy && (long)(millis() - fwNextCheck) >= 0) {
    bool ok = checkFirmware();
    fwNextCheck = millis() + (ok ? FW_CHECK_EVERY : FW_CHECK_RETRY);
    // Auto-install : seulement une version officielle (pas un build "dev" local)
    if (ok && fwUpdateAvailable && autoUpdate && strcmp(FW_VERSION, "dev") != 0) {
      addLog("MAJ automatique...");
      doFirmwareUpdate();
      return;
    }
  }

  // Poll NHL — intervalle dynamique (désactivé en mode hors-saison)
  if (!offSeason && (millis() - lastPoll >= pollInterval || lastPoll == 0)) {
    lastPoll = millis();
    checkScore();
  }

  delay(50);
}

// ─── MATCH EN CACHE ────────────────────────────────────────────────────────
// Une fois le match du jour trouvé, on ne relit plus l'horaire de la semaine
// (gros JSON) : pendant le match on poll seulement /landing.
String curGameId   = "";
String curGameDate = "";

#define SCHED_ERROR 0   // erreur réseau / JSON → on garde l'état actuel
#define SCHED_NONE  1   // aucun match (ou match terminé)
#define SCHED_FOUND 2   // match du jour trouvé

// Date de "la journée de match" : avant 4h AM on reste sur la veille
// (match du soir qui déborde minuit)
void gameDate(char* out) {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  if (ti.tm_hour < 4) {
    now -= 86400;
    localtime_r(&now, &ti);
  }
  strftime(out, 11, "%Y-%m-%d", &ti);
}

int readSchedule(const char* dateStr, String& gameIdOut);
void fetchGameScore(const String& gameId);

// ─── CHECK SCORE ───────────────────────────────────────────────────────────
void checkScore() {
  if (WiFi.status() != WL_CONNECTED) {
    addLog("WiFi perdu — reconnexion...");
    WiFi.reconnect();
    delay(5000);
    if (WiFi.status() != WL_CONNECTED) ESP.restart();
    setPollInterval(POLL_RETRY);
    return;
  }

  char dateStr[11];
  gameDate(dateStr);

  // Match déjà en cache et en cours → directement le score, pas d'horaire
  if (curGameId != "" && curGameDate == dateStr && pollInterval == POLL_LIVE) {
    fetchGameScore(curGameId);
    return;
  }

  String id;
  int r = readSchedule(dateStr, id);

  if (r == SCHED_ERROR) {
    // Erreur réseau/JSON : on NE touche PAS à lastScore ni gameActive.
    // Sinon un but compté pendant le trou serait absorbé par la réinitialisation.
    return;
  }

  if (r == SCHED_NONE) {
    addLog("Pas de game aujourd'hui.");
    curGameId  = "";
    lastScore  = -1;
    gameActive = false;
    return;
  }

  // SCHED_FOUND
  curGameId   = id;
  curGameDate = dateStr;
  if (pollInterval == POLL_LIVE) fetchGameScore(id);  // PRE / LIVE / CRIT
}

// ─── LECTURE DE L'HORAIRE ──────────────────────────────────────────────────
int readSchedule(const char* dateStr, String& gameIdOut) {
  String url = String("https://api-web.nhle.com/v1/schedule/") + dateStr;
  addLog("Poll schedule: %s", dateStr);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);          // pas de chunked → lecture en streaming
  http.setTimeout(15000);
  http.begin(client, url);
  int code = http.GET();
  if (code != 200) {
    addLog("HTTP schedule erreur: %d", code);
    http.end();
    setPollInterval(POLL_RETRY);
    return SCHED_ERROR;
  }
  client.setTimeout(READ_TIMEOUT); // délai de LECTURE du flux — défaut ~1 s

  JsonDocument filter;
  filter["gameWeek"][0]["date"] = true;
  JsonObject fg = filter["gameWeek"][0]["games"][0].to<JsonObject>();
  fg["id"]             = true;
  fg["gameState"]      = true;
  fg["homeTeam"]["id"] = true;
  fg["awayTeam"]["id"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    addLog("JSON erreur schedule: %s", err.c_str());
    setPollInterval(POLL_RETRY);
    return SCHED_ERROR;
  }

  for (JsonObject day : doc["gameWeek"].as<JsonArray>()) {
    if (strcmp(day["date"] | "", dateStr) != 0) continue;   // aujourd'hui seulement
    for (JsonObject game : day["games"].as<JsonArray>()) {
      int homeId = game["homeTeam"]["id"].as<int>();
      int awayId = game["awayTeam"]["id"].as<int>();
      if (homeId != HABS_TEAM_ID && awayId != HABS_TEAM_ID) continue;

      String id    = game["id"].as<String>();
      String state = game["gameState"].as<String>();
      addLog("Game trouvee: %s  etat: %s", id.c_str(), state.c_str());

      if (state == "LIVE" || state == "CRIT" || state == "PRE") {
        gameActive = (state == "LIVE" || state == "CRIT");
        setPollInterval(POLL_LIVE);        // 15 sec
        gameIdOut = id;
        return SCHED_FOUND;
      }
      if (state == "FUT") {
        gameActive = false;
        setPollInterval(POLL_SCHEDULED);   // 5 min jusqu'au PRE
        gameIdOut = id;
        return SCHED_FOUND;
      }
      setPollInterval(POLL_IDLE);          // FINAL / OFF → match terminé
      return SCHED_NONE;
    }
  }
  setPollInterval(POLL_IDLE);              // aucun match aujourd'hui
  return SCHED_NONE;
}

// ─── FETCH GAME SCORE ──────────────────────────────────────────────────────
void fetchGameScore(const String& gameId) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  String url = "https://api-web.nhle.com/v1/gamecenter/" + gameId + "/landing";
  http.begin(client, url);
  int code = http.GET();
  if (code != 200) {
    addLog("HTTP score erreur: %d", code);
    http.end();
    return;                      // état conservé, réessai au prochain poll
  }
  client.setTimeout(READ_TIMEOUT);

  JsonDocument filter;
  filter["gameState"]         = true;
  filter["homeTeam"]["id"]    = true;
  filter["homeTeam"]["score"] = true;
  filter["awayTeam"]["id"]    = true;
  filter["awayTeam"]["score"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    addLog("JSON erreur score: %s", err.c_str());
    return;                      // état conservé, réessai au prochain poll
  }

  int homeId    = doc["homeTeam"]["id"].as<int>();
  int habsScore = (homeId == HABS_TEAM_ID)
                    ? doc["homeTeam"]["score"].as<int>()
                    : doc["awayTeam"]["score"].as<int>();

  String gameState = doc["gameState"].as<String>();
  bool   finished  = (gameState == "FINAL" || gameState == "OFF");
  gameActive = (gameState == "LIVE" || gameState == "CRIT");

  addLog("Score Canadiens: %d  (%s)", habsScore, gameState.c_str());

  if (lastScore == -1) {
    lastScore = habsScore;
    addLog("Score initialise a %d.", habsScore);
  } else if (habsScore > lastScore) {
    // Pas de condition gameActive : un but en prolongation peut arriver
    // dans le même poll où l'état passe à FINAL
    addLog("*** BUT! Score: %d -> %d ***", lastScore, habsScore);
    lastScore     = habsScore;
    goalFlashing  = true;
    goalStartTime = millis();
  } else {
    lastScore = habsScore;
  }

  if (finished) {
    addLog("Match termine.");
    curGameId = "";
    lastScore = -1;
    setPollInterval(POLL_IDLE);
  }
}
