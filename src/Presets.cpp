#include "Controller.h"
#include "ControllerLogic.h"
#include <LittleFS.h>

char presetRamCache[PRESET_COUNT][NAME_SIZE] = {};
CacheState presetCacheState[PRESET_COUNT] = {};
bool cacheIsDirty = false, storageReady = false;
uint32_t cacheRevision = 1;
bool isScanning = false, scanDeepMode = false;
int scanCurrentPreset = 0;
bool presetPending = false, presetReady = false;
static bool scanWaiting = false, scanReceived = false;
static int scanRestorePreset = -1;
static unsigned long scanStarted = 0, presetRequested = 0, lastPoll = 0;
static unsigned long lastConfirmed = 0;
static int nextSceneName = 0, awaitedSceneName = 0;
static unsigned long sceneRequested = 0;

bool validPreset(int number) { return number >= 0 && number < PRESET_COUNT; }
void copyName(char* target, const char* name) {
  strncpy(target, name ? name : "", NAME_SIZE - 1);
  target[NAME_SIZE - 1] = '\0';
}

static void invalidateScenes() {
  memset(sceneNames, 0, sizeof(sceneNames));
  currentSceneNumber = -1;
  copyName(currentSceneName, "---");
  nextSceneName = awaitedSceneName = 0;
  for (auto& slot : slots) slot.available = slot.active = false;
}

void selectPreset(int number) {
  if (!validPreset(number)) return;
  currentPresetNumber = number;
  copyName(currentPresetName, presetCacheState[number] == CacheState::Known ? presetRamCache[number] : "---");
  invalidateScenes();
  presetPending = true;
  presetReady = false;
  presetRequested = millis();
  scrollOffset = 0;
  displayDirty = true;
  Axe.sendPresetChange(number);
}

void setCurrentSceneLocal(SceneNumber scene) {
  if (scene < 1 || scene > AxeSystem::MAX_SCENES) return;
  if (currentSceneNumber != scene || strcmp(currentSceneName, sceneNames[scene - 1])) displayDirty = true;
  currentSceneNumber = scene;
  copyName(currentSceneName, sceneNames[scene - 1]);
}

void selectScene(int number) {
  if (!presetReady || presetPending || number < 1 || number > AxeSystem::MAX_SCENES) return;
  Axe.sendSceneChange(number);
  // The confirmed scene number is adopted in onPresetChanged().
}

void requestAllSceneNames() {
  if (!presetReady || isScanning || nextSceneName || awaitedSceneName) return;
  nextSceneName = 1;
}

void updateSlotStatesFromAxe() {
  if (!presetReady || presetPending) return;
  AxePreset& preset = Axe.getCurrentPreset();
  if (preset.getPresetNumber() != currentPresetNumber) return;
  for (auto& slot : slots) {
    bool available = preset.hasEffect(slot.effectId);
    bool active = available && Axe.isEffectEnabled(slot.effectId);
    if (available != slot.available || active != slot.active) displayDirty = true;
    slot.available = available;
    slot.active = active;
  }
}

static void onPresetChanging(PresetNumber number) {
  if (!validPreset(number)) return;
  currentPresetNumber = number;
  copyName(currentPresetName, presetCacheState[number] == CacheState::Known ? presetRamCache[number] : "---");
  invalidateScenes();
  presetReady = false;
  displayDirty = true;
  presetPending = true;
  presetRequested = millis();
}

static void onPresetName(PresetNumber number, const char* name, byte length) {
  if (!validPreset(number)) return;
  if (presetPending && number != currentPresetNumber) return;
  char bounded[NAME_SIZE] = {};
  size_t size = length < NAME_SIZE - 1 ? length : NAME_SIZE - 1;
  memcpy(bounded, name, size);
  if (presetCacheState[number] != CacheState::Known || strcmp(presetRamCache[number], bounded)) {
    copyName(presetRamCache[number], bounded);
    presetCacheState[number] = CacheState::Known;
    cacheIsDirty = true;
    ++cacheRevision;
  }
  if (isScanning && scanWaiting && number == scanCurrentPreset) scanReceived = true;
  if (number != currentPresetNumber) {
    currentPresetNumber = number;
    invalidateScenes();
    presetReady = false;
  }
  copyName(currentPresetName, bounded);
  displayDirty = true;
}

static void onSceneName(SceneNumber number, const char* name, byte length) {
  if (number < 1 || number > AxeSystem::MAX_SCENES) return;
  size_t size = length < NAME_SIZE - 1 ? length : NAME_SIZE - 1;
  char previous[NAME_SIZE];
  copyName(previous, sceneNames[number - 1]);
  memset(sceneNames[number - 1], 0, NAME_SIZE);
  memcpy(sceneNames[number - 1], name, size);
  if (number == currentSceneNumber) copyName(currentSceneName, sceneNames[number - 1]);
  if (number == awaitedSceneName) awaitedSceneName = 0;
  if (strcmp(previous, sceneNames[number - 1])) displayDirty = true;
}

static void onPresetChanged(AxePreset preset) {
  if (preset.getPresetNumber() != currentPresetNumber) return;
  bool firstDetails = !presetReady;
  presetPending = false;
  presetReady = true;
  lastConfirmed = millis();
  if (lastOperation == "MIDI-Verbindung unterbrochen" || lastOperation == "Keine vollstaendige MIDI-Antwort") lastOperation = "MIDI-Verbindung hergestellt";
  setCurrentSceneLocal(preset.getSceneNumber());
  updateSlotStatesFromAxe();
  if (firstDetails) requestAllSceneNames();
}

void setupMidi() {
  MidiSerial.setRxBufferSize(2048);
  MidiSerial.begin(31250, SERIAL_8N1, 16, 17);
  Axe.setStartupDelay(0);
  Axe.begin(MidiSerial);
  Axe.fetchEffects(true);
  Axe.registerPresetChangingCallback(onPresetChanging);
  Axe.registerPresetNameCallback(onPresetName);
  Axe.registerSceneNameCallback(onSceneName);
  Axe.registerPresetChangeCallback(onPresetChanged);
  Axe.requestPresetDetails();
}

void loadPresetCache() {
  storageReady = LittleFS.begin(false); // Never format user data on a mount failure.
  if (!storageReady) { lastOperation = "Preset-Speicher nicht verfuegbar"; return; }
  File file = LittleFS.open("/presets.bin", FILE_READ);
  if (file) {
    const size_t expected = 8 + PRESET_COUNT * (NAME_SIZE + 1);
    char magic[8];
    bool ok = file.size() == expected && file.readBytes(magic, 8) == 8 && !memcmp(magic, "LEPUS001", 8);
    for (int i = 0; ok && i < PRESET_COUNT; ++i) {
      int state = file.read();
      ok = state >= 0 && state <= int(CacheState::Timeout) && file.readBytes(presetRamCache[i], NAME_SIZE) == NAME_SIZE;
      presetRamCache[i][NAME_SIZE - 1] = '\0';
      presetCacheState[i] = CacheState(state);
    }
    file.close();
    if (ok) return;
    memset(presetRamCache, 0, sizeof(presetRamCache));
    memset(presetCacheState, 0, sizeof(presetCacheState));
    lastOperation = "Preset-Datei beschaedigt; alter Cache wird versucht";
  }
  file = LittleFS.open("/presets.txt", FILE_READ);
  if (!file) return;
  for (int i = 0; i < PRESET_COUNT && file.available(); ++i) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line == "[Leer/Timeout]") presetCacheState[i] = CacheState::Timeout;
    else if (line.length() && line != "---") {
      copyName(presetRamCache[i], line.c_str());
      presetCacheState[i] = CacheState::Known;
    }
  }
  file.close();
  cacheIsDirty = true; // Migrate the legacy cache on the next explicit save.
}

void savePresetCache() {
  if (!storageReady) { lastOperation = "Speichern fehlgeschlagen: Speicher nicht verfuegbar"; return; }
  if (!cacheIsDirty) { lastOperation = "Namen bereits gespeichert"; return; }
  // All cache mutations and saves run in loop(), so no update can be lost here.
  File file = LittleFS.open("/presets.tmp", FILE_WRITE);
  bool ok = bool(file);
  if (ok) ok = file.write(reinterpret_cast<const uint8_t*>("LEPUS001"), 8) == 8;
  for (int i = 0; ok && i < PRESET_COUNT; ++i) {
    ok = file.write(uint8_t(presetCacheState[i])) == 1 &&
         file.write(reinterpret_cast<const uint8_t*>(presetRamCache[i]), NAME_SIZE) == NAME_SIZE;
  }
  if (file) { file.flush(); ok = ok && !file.getWriteError(); file.close(); }
  if (ok) ok = LittleFS.rename("/presets.tmp", "/presets.bin");
  if (ok) cacheIsDirty = false;
  lastOperation = ok ? "Preset-Namen gespeichert" : "Speichern fehlgeschlagen; bisherige Datei bleibt erhalten";
}

void startScan(bool deep) {
  if (isScanning) return;
  isScanning = true;
  scanDeepMode = deep;
  scanRestorePreset = currentPresetNumber;
  scanCurrentPreset = 0;
  scanWaiting = scanReceived = false;
  nextSceneName = awaitedSceneName = 0;
  lastOperation = deep ? "Vollstaendiger Scan gestartet" : "Scan unbekannter Presets gestartet";
}

void stopScan() {
  if (!isScanning) return;
  isScanning = false;
  scanWaiting = false;
  if (validPreset(scanRestorePreset)) selectPreset(scanRestorePreset);
  else Axe.requestPresetDetails();
  displayDirty = true;
}

void updatePresets(unsigned long now) {
  if (isScanning) {
    if (scanWaiting) {
      if (scanDecision(scanReceived, now, scanStarted) == ScanDecision::Wait) return;
      if (!scanReceived && presetCacheState[scanCurrentPreset] != CacheState::Known) {
        presetCacheState[scanCurrentPreset] = CacheState::Timeout;
        cacheIsDirty = true;
        ++cacheRevision;
      }
      scanWaiting = false;
      ++scanCurrentPreset;
    }
    while (scanCurrentPreset < PRESET_COUNT && !scanDeepMode && presetCacheState[scanCurrentPreset] == CacheState::Known) ++scanCurrentPreset;
    if (scanCurrentPreset == PRESET_COUNT) {
      stopScan();
      lastOperation = "Scan beendet; vorheriges Preset wiederhergestellt";
      return;
    }
    scanWaiting = true;
    scanReceived = false;
    scanStarted = now;
    selectPreset(scanCurrentPreset);
    return;
  }
  if (presetPending && uint32_t(now - presetRequested) >= 3000) {
    presetPending = false;
    presetReady = false;
    nextSceneName = awaitedSceneName = 0;
    lastOperation = "Keine vollstaendige MIDI-Antwort";
  }
  if (presetReady && uint32_t(now - lastConfirmed) >= 5000) {
    presetReady = false;
    nextSceneName = awaitedSceneName = 0;
    for (auto& slot : slots) slot.available = slot.active = false;
    displayDirty = true;
    lastOperation = "MIDI-Verbindung unterbrochen";
  }
  if (awaitedSceneName && uint32_t(now - sceneRequested) >= 400) awaitedSceneName = 0;
  if (!presetPending && presetReady && nextSceneName && !awaitedSceneName) {
    awaitedSceneName = nextSceneName++;
    if (nextSceneName > AxeSystem::MAX_SCENES) nextSceneName = 0;
    sceneRequested = now;
    Axe.requestSceneName(awaitedSceneName);
  }
  if (!presetPending && !nextSceneName && !awaitedSceneName && uint32_t(now - lastPoll) >= 2000) {
    lastPoll = now;
    Axe.requestPresetDetails(); // Also discovers changes when MIDI PC transmission is off.
  }
}
