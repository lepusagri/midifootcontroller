#include "Controller.h"
#include "ControllerLogic.h"
#include "WebPage.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <freertos/semphr.h>

static AsyncWebServer server(80);
static SemaphoreHandle_t snapshotMutex;
static String statusSnapshot = "{}", cacheSnapshot = "{}";
static uint32_t publishedCacheRevision = 0;

static String jsonString(const char* value) {
  String result;
  appendJsonString(result, value);
  return result;
}

void publishWebState() {
  if (!snapshotMutex) return;
  String json;
  json.reserve(7500);
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
  const char* mode = currentMode == MODE_EFFECTS ? "effects" : currentMode == MODE_SCENES ? "scenes" :
                     currentMode == MODE_PRESETS ? "presets" : "customMidi";
  json += ",\"mode\":" + jsonString(mode);
  json += ",\"customMidiRevision\":" + String(customMidiRevision);
  json += ",\"customMidiDirty\":" + String(customMidiDirty ? "true" : "false");
  json += ",\"customMidi\":[";
  for (uint8_t slot = 0; slot < NUM_SWITCHES; ++slot) {
    if (slot) json += ',';
    const CustomMidiSwitch& setting = customMidiSwitches[slot];
    json += "{\"mode\":\"";
    json += setting.mode == CustomMidiMode::Alternate ? "alternate" : "single";
    json += "\",\"name\":" + jsonString(setting.name);
    json += ",\"nextBank\":" + String(setting.nextBank);
    json += ",\"banks\":[";
    for (uint8_t bank = 0; bank < CUSTOM_MIDI_BANKS; ++bank) {
      if (bank) json += ',';
      json += '[';
      for (uint8_t index = 0; index < setting.count[bank]; ++index) {
        if (index) json += ',';
        const CustomMidiCommand& midi = setting.commands[bank][index];
        json += "{\"cmd\":\"CC\",\"channel\":" + String(midi.channel);
        json += ",\"number\":" + String(midi.number);
        json += ",\"value\":" + String(midi.value) + '}';
      }
      json += ']';
    }
    json += "]}";
  }
  json += ']';
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
  server.on("/api/custom-midi/mode", HTTP_POST, [](AsyncWebServerRequest* r) {
    int slot, mode;
    if (!numberParam(r, "slot", 0, NUM_SWITCHES - 1, slot) ||
        !numberParam(r, "mode", 0, 1, mode)) return;
    enqueue(r, {CommandType::CustomMidiSetMode, slot, mode});
  });
  server.on("/api/custom-midi/name", HTTP_POST, [](AsyncWebServerRequest* r) {
    // +++++++++++++++++++++++++++++++++
    int iSlot = 0;
    const AsyncWebParameter* ptParameter = nullptr;
    String sName;
    Command tCommand{CommandType::CustomMidiSetName, 0};
    // +++++++++++++++++++++++++++++++++
    if (!numberParam(r, "slot", 0, NUM_SWITCHES - 1, iSlot)) return;
    ptParameter = r->getParam("name");
    if (!ptParameter) { r->send(400, "application/json", "{\"error\":\"Name fehlt\"}"); return; }
    sName = ptParameter->value();
    sName.trim();
    if (sName.length() > CUSTOM_MIDI_NAME_LENGTH) {
      r->send(400, "application/json", "{\"error\":\"Name zu lang\"}");
      return;
    }
    for (size_t iIndex = 0; iIndex < sName.length(); ++iIndex) {
      if (sName[iIndex] < ' ' || sName[iIndex] > '~') {
        r->send(400, "application/json", "{\"error\":\"Nur darstellbare ASCII-Zeichen erlaubt\"}");
        return;
      }
    }
    tCommand.value = iSlot;
    memcpy(tCommand.name, sName.c_str(), sName.length() + 1);
    enqueue(r, tCommand);
  });
  server.on("/api/custom-midi/command", HTTP_POST, [](AsyncWebServerRequest* r) {
    int slot, bank, index, channel, number, value;
    if (!numberParam(r, "slot", 0, NUM_SWITCHES - 1, slot) ||
        !numberParam(r, "bank", 0, CUSTOM_MIDI_BANKS - 1, bank) ||
        !numberParam(r, "index", 0, 0, index) ||
        !numberParam(r, "channel", 1, 16, channel) ||
        !numberParam(r, "number", 0, 127, number) ||
        !numberParam(r, "value", 0, 127, value)) return;
    enqueue(r, {CommandType::CustomMidiSetCommand, slot, bank, index, channel, number, value});
  });
  server.on("/api/custom-midi/remove", HTTP_POST, [](AsyncWebServerRequest* r) {
    int slot, bank, index;
    if (!numberParam(r, "slot", 0, NUM_SWITCHES - 1, slot) ||
        !numberParam(r, "bank", 0, CUSTOM_MIDI_BANKS - 1, bank) ||
        !numberParam(r, "index", 0, 0, index)) return;
    enqueue(r, {CommandType::CustomMidiRemoveCommand, slot, bank, index});
  });
  server.on("/api/custom-midi/save", HTTP_POST, [](AsyncWebServerRequest* r) {
    enqueue(r, {CommandType::CustomMidiSave, 0});
  });
  server.on("/api/scan/start", HTTP_POST, [](AsyncWebServerRequest* r) {
    auto* parameter = r->getParam("mode");
    String value = parameter ? parameter->value() : "";
    if (value != "smart" && value != "deep") { r->send(400, "application/json", "{\"error\":\"Ungueltiger Scanmodus\"}"); return; }
    enqueue(r, {value == "deep" ? CommandType::ScanDeep : CommandType::ScanSmart, 0});
  });
  server.on("/api/scan/stop", HTTP_POST, [](AsyncWebServerRequest* r) { enqueue(r, {CommandType::ScanStop, 0}); });
  server.on("/api/network", HTTP_GET, [](AsyncWebServerRequest* r) {
    auto* response = r->beginResponse(200, "application/json; charset=utf-8", networkSettingsJson());
    response->addHeader("Cache-Control", "no-store");
    r->send(response);
  });
  server.on("/api/network", HTTP_POST, [](AsyncWebServerRequest* r) {
    auto* mode = r->getParam("mode", true);
    auto* ssid = r->getParam("ssid", true);
    auto* password = r->getParam("password", true);
    auto* dhcp = r->getParam("dhcp", true);
    auto* ip = r->getParam("ip", true);
    auto* gateway = r->getParam("gateway", true);
    auto* subnet = r->getParam("subnet", true);
    auto* dns = r->getParam("dns", true);
    if (!mode || (mode->value() != "ap" && mode->value() != "home") ||
        (mode->value() == "home" && (!ssid || !password || !dhcp || !ip || !gateway || !subnet || !dns ||
        (dhcp->value() != "0" && dhcp->value() != "1")))) {
      r->send(400, "application/json", "{\"error\":\"Ungueltige Netzwerkeinstellungen\"}");
      return;
    }
    String error;
    if (!saveNetworkSettings(mode->value() == "ap", ssid ? ssid->value() : "",
                             password ? password->value() : "", dhcp && dhcp->value() == "1",
                             ip ? ip->value() : "", gateway ? gateway->value() : "",
                             subnet ? subnet->value() : "", dns ? dns->value() : "", error)) {
      String response = "{\"error\":" + jsonString(error.c_str()) + "}";
      r->send(400, "application/json", response);
      return;
    }
    r->send(202, "application/json", "{\"accepted\":true,\"restart\":true}");
  });
  server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "application/json", "{\"error\":\"Route nicht gefunden\"}"); });
  setupNetwork();
  server.begin();
}
