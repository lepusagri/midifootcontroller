#include "Controller.h"
#include "ControllerLogic.h"
#include "WebPage.h"
#include "NetworkConfig.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <freertos/semphr.h>

static AsyncWebServer server(80);
static SemaphoreHandle_t snapshotMutex;
static String statusSnapshot = "{}", cacheSnapshot = "{}";
static uint32_t publishedCacheRevision = 0;
static bool serverStarted = false, mdnsStarted = false;

static String jsonString(const char* value) {
  String result;
  appendJsonString(result, value);
  return result;
}

void publishWebState() {
  if (!snapshotMutex) return;
  String json;
  json.reserve(2200);
  json = "{\"scanning\":" + String(isScanning ? "true" : "false");
  json += ",\"scanProgress\":" + String(scanCurrentPreset);
  json += ",\"presetCount\":" + String(PRESET_COUNT);
  json += ",\"cacheRevision\":" + String(cacheRevision);
  json += ",\"unsaved\":" + String(cacheIsDirty ? "true" : "false");
  json += ",\"storageReady\":" + String(storageReady ? "true" : "false");
  json += ",\"ready\":" + String(presetReady && !presetPending ? "true" : "false");
  json += ",\"presetNumber\":" + String(currentPresetNumber);
  json += ",\"presetName\":" + jsonString(currentPresetName);
  json += ",\"sceneNumber\":" + String(currentSceneNumber);
  json += ",\"sceneName\":" + jsonString(currentSceneName);
  json += ",\"message\":" + jsonString(lastOperation.c_str());
  json += ",\"favoriteModeEnabled\":" + String(favoritePresetModeEnabled ? "true" : "false");
  json += ",\"favoriteModeActive\":" + String(usesFavoritePresetMode() ? "true" : "false");
  json += ",\"favoritesRevision\":" + String(favoritesRevision);
  json += ",\"favorites\":[";
  for (int i = 0; i < NUM_SWITCHES; ++i) {
    if (i) json += ',';
    json += String(favoritePresets[i]);
  }
  json += ']';
  const char* mode = currentMode == MODE_EFFECTS ? "effects" : currentMode == MODE_SCENES ? "scenes" : "presets";
  json += ",\"mode\":" + jsonString(mode);
  json += ",\"scenes\":[";
  for (int i = 0; i < AxeSystem::MAX_SCENES; ++i) {
    if (i) json += ',';
    json += "{\"number\":" + String(i + 1) + ",\"name\":" + jsonString(sceneNames[i]);
    json += ",\"active\":" + String(currentSceneNumber == i + 1 ? "true" : "false") + "}";
  }
  json += "],\"slots\":[";
  for (int i = 0; i < NUM_SWITCHES; ++i) {
    if (i) json += ',';
    json += "{\"index\":" + String(i) + ",\"label\":" + jsonString(slots[i].label);
    json += ",\"available\":" + String(slots[i].available ? "true" : "false");
    json += ",\"active\":" + String(slots[i].active ? "true" : "false") + "}";
  }
  json += "]}";
  String cache;
  const bool changed = cacheRevision != publishedCacheRevision;
  if (changed) {
    cache.reserve(PRESET_COUNT * (NAME_SIZE + 25));
    cache = "{\"revision\":" + String(cacheRevision) + ",\"presets\":[";
    for (int i = 0; i < PRESET_COUNT; ++i) {
      if (i) cache += ',';
      cache += "{\"name\":" + jsonString(presetRamCache[i]) + ",\"state\":" + String(int(presetCacheState[i])) + "}";
    }
    cache += "]}";
  }
  xSemaphoreTake(snapshotMutex, portMAX_DELAY);
  statusSnapshot = json;
  if (changed) { cacheSnapshot = cache; publishedCacheRevision = cacheRevision; }
  xSemaphoreGive(snapshotMutex);
}

static void sendSnapshot(AsyncWebServerRequest* request, bool cache) {
  if (!snapshotMutex || xSemaphoreTake(snapshotMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    request->send(503, "application/json", "{\"error\":\"Status momentan nicht verfuegbar\"}");
    return;
  }
  String snapshot = cache ? cacheSnapshot : statusSnapshot;
  xSemaphoreGive(snapshotMutex);
  auto* response = request->beginResponse(200, "application/json; charset=utf-8", snapshot);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

static void enqueue(AsyncWebServerRequest* request, Command command) {
  if (!commandQueue || xQueueSend(commandQueue, &command, 0) != pdTRUE) {
    request->send(503, "application/json", "{\"error\":\"Befehlsqueue voll\"}");
    return;
  }
  request->send(202, "application/json", "{\"accepted\":true}");
}

static bool numberParam(AsyncWebServerRequest* request, const char* key, int min, int max, int& value) {
  auto* parameter = request->getParam(key);
  if (!parameter || !parseUnsigned(parameter->value().c_str(), max, value) || value < min) {
    request->send(400, "application/json", "{\"error\":\"Fehlender oder ungueltiger Parameter\"}");
    return false;
  }
  return true;
}

void setupWebServer() {
  snapshotMutex = xSemaphoreCreateMutex();
  publishWebState();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    auto* response = r->beginResponse(
      200,
      "text/html; charset=utf-8",
      reinterpret_cast<const uint8_t*>(WEB_PAGE),
      WEB_PAGE_LENGTH);
    response->addHeader("Cache-Control", "no-store");
    r->send(response);
  });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) { sendSnapshot(r, false); });
  server.on("/api/presets", HTTP_GET, [](AsyncWebServerRequest* r) { sendSnapshot(r, true); });
  server.on("/api/preset", HTTP_POST, [](AsyncWebServerRequest* r) {
    int value;
    if (numberParam(r, "number", 0, LAST_PRESET, value)) enqueue(r, {CommandType::Preset, value});
  });
  server.on("/api/effect", HTTP_POST, [](AsyncWebServerRequest* r) {
    int value;
    if (numberParam(r, "slot", 0, NUM_SWITCHES - 1, value)) enqueue(r, {CommandType::Effect, value});
  });
  server.on("/api/scene", HTTP_POST, [](AsyncWebServerRequest* r) {
    int value;
    if (numberParam(r, "number", 1, AxeSystem::MAX_SCENES, value)) enqueue(r, {CommandType::Scene, value});
  });
  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest* r) {
    auto* parameter = r->getParam("cmd");
    String value = parameter ? parameter->value() : "";
    CommandType type;
    if (value == "preset_up") type = CommandType::PresetUp;
    else if (value == "preset_down") type = CommandType::PresetDown;
    else if (value == "scene_up") type = CommandType::SceneUp;
    else if (value == "scene_down") type = CommandType::SceneDown;
    else { r->send(400, "application/json", "{\"error\":\"Unbekannter Befehl\"}"); return; }
    enqueue(r, {type, 0});
  });
  server.on("/api/save", HTTP_POST, [](AsyncWebServerRequest* r) { enqueue(r, {CommandType::Save, 0}); });
  server.on("/api/favorite/toggle", HTTP_POST, [](AsyncWebServerRequest* r) {
    int value;
    if (numberParam(r, "number", 0, LAST_PRESET, value)) enqueue(r, {CommandType::FavoriteToggle, value});
  });
  server.on("/api/favorite/move", HTTP_POST, [](AsyncWebServerRequest* r) {
    int slot;
    int direction;
    auto* parameter = r->getParam("direction");
    const String value = parameter ? parameter->value() : "";
    if (!numberParam(r, "slot", 0, NUM_SWITCHES - 1, slot)) return;
    if (value == "-1") direction = -1;
    else if (value == "1") direction = 1;
    else { r->send(400, "application/json", "{\"error\":\"Ungueltige Richtung\"}"); return; }
    enqueue(r, {CommandType::FavoriteMove, slot, direction});
  });
  server.on("/api/favorite/mode", HTTP_POST, [](AsyncWebServerRequest* r) {
    int value;
    if (numberParam(r, "enabled", 0, 1, value)) enqueue(r, {CommandType::FavoriteMode, value});
  });
  server.on("/api/scan/start", HTTP_POST, [](AsyncWebServerRequest* r) {
    auto* parameter = r->getParam("mode");
    String value = parameter ? parameter->value() : "";
    if (value != "smart" && value != "deep") { r->send(400, "application/json", "{\"error\":\"Ungueltiger Scanmodus\"}"); return; }
    enqueue(r, {value == "deep" ? CommandType::ScanDeep : CommandType::ScanSmart, 0});
  });
  server.on("/api/scan/stop", HTTP_POST, [](AsyncWebServerRequest* r) { enqueue(r, {CommandType::ScanStop, 0}); });
  server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "application/json", "{\"error\":\"Route nicht gefunden\"}"); });
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void updateNetwork(unsigned long now) {
  static unsigned long lastRetry = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (!serverStarted) { server.begin(); serverStarted = true; Serial.println(WiFi.localIP()); }
    if (!mdnsStarted && MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); mdnsStarted = true; }
  } else {
    if (mdnsStarted) { MDNS.end(); mdnsStarted = false; }
    if (now - lastRetry >= 30000) { lastRetry = now; WiFi.reconnect(); }
  }
}
