#include "Controller.h"
#include "ControllerLogic.h"
#include <LittleFS.h>

int favoritePresets[NUM_SWITCHES] = {
  EMPTY_FAVORITE, EMPTY_FAVORITE, EMPTY_FAVORITE,
  EMPTY_FAVORITE, EMPTY_FAVORITE, EMPTY_FAVORITE
};
bool favoritePresetModeEnabled = false;
uint32_t favoritesRevision = 1;

static bool saveFavoriteSettings() {
  if (!storageReady) {
    lastOperation = "Favoriten konnten nicht gespeichert werden";
    return false;
  }
  File file = LittleFS.open("/favorites.tmp", FILE_WRITE);
  bool ok = bool(file);
  if (ok) ok = file.write(reinterpret_cast<const uint8_t*>("LEPFAV01"), 8) == 8;
  if (ok) ok = file.write(uint8_t(favoritePresetModeEnabled ? 1 : 0)) == 1;
  for (uint8_t slot = 0; ok && slot < NUM_SWITCHES; ++slot) {
    const int16_t value = int16_t(favoritePresets[slot]);
    ok = file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
  }
  if (file) { file.flush(); ok = ok && !file.getWriteError(); file.close(); }
  if (ok) ok = LittleFS.rename("/favorites.tmp", "/favorites.bin");
  if (!ok) lastOperation = "Favoriten konnten nicht gespeichert werden";
  return ok;
}

void loadFavoriteSettings() {
  if (!storageReady) return;
  File file = LittleFS.open("/favorites.bin", FILE_READ);
  if (!file) return;
  const size_t expected = 8 + 1 + NUM_SWITCHES * sizeof(int16_t);
  char magic[8];
  bool ok = file.size() == expected && file.readBytes(magic, 8) == 8 && !memcmp(magic, "LEPFAV01", 8);
  const int enabled = ok ? file.read() : -1;
  ok = ok && (enabled == 0 || enabled == 1);
  int loaded[NUM_SWITCHES];
  for (uint8_t slot = 0; ok && slot < NUM_SWITCHES; ++slot) {
    int16_t value = EMPTY_FAVORITE;
    ok = file.readBytes(reinterpret_cast<char*>(&value), sizeof(value)) == sizeof(value);
    ok = ok && (value == EMPTY_FAVORITE || validPreset(value));
    loaded[slot] = value;
  }
  file.close();
  if (!ok) {
    lastOperation = "Favoriten-Datei ist beschaedigt";
    return;
  }
  favoritePresetModeEnabled = enabled != 0;
  memcpy(favoritePresets, loaded, sizeof(favoritePresets));
}

void toggleFavorite(int number) {
  if (!validPreset(number)) return;
  for (uint8_t slot = 0; slot < NUM_SWITCHES; ++slot) {
    if (favoritePresets[slot] != number) continue;
    favoritePresets[slot] = EMPTY_FAVORITE;
    ++favoritesRevision;
    displayDirty = true;
    if (saveFavoriteSettings()) lastOperation = "Favorit entfernt";
    return;
  }
  for (uint8_t slot = 0; slot < NUM_SWITCHES; ++slot) {
    if (favoritePresets[slot] != EMPTY_FAVORITE) continue;
    favoritePresets[slot] = number;
    ++favoritesRevision;
    displayDirty = true;
    if (saveFavoriteSettings()) lastOperation = "Favorit hinzugefuegt";
    return;
  }
  lastOperation = "Alle sechs Favoritenplaetze sind belegt";
}

void moveFavorite(uint8_t slot, int direction) {
  if (slot >= NUM_SWITCHES || (direction != -1 && direction != 1)) return;
  const int other = int(slot) + direction;
  if (other < 0 || other >= NUM_SWITCHES) return;
  const int value = favoritePresets[slot];
  favoritePresets[slot] = favoritePresets[other];
  favoritePresets[other] = value;
  ++favoritesRevision;
  displayDirty = true;
  if (saveFavoriteSettings()) lastOperation = "Favoriten-Reihenfolge gespeichert";
}

void setFavoritePresetMode(bool enabled) {
  if (favoritePresetModeEnabled == enabled) return;
  favoritePresetModeEnabled = enabled;
  ++favoritesRevision;
  displayDirty = true;
  if (saveFavoriteSettings()) {
    lastOperation = enabled ? "Favoritenmodus aktiviert" : "Relativer Presetmodus aktiviert";
  }
}

bool usesFavoritePresetMode() {
  if (!favoritePresetModeEnabled) return false;
  for (uint8_t slot = 0; slot < NUM_SWITCHES; ++slot) {
    if (validPreset(favoritePresets[slot])) return true;
  }
  return false;
}

int presetForSwitch(uint8_t index) {
  return favoritePresetForSwitch(currentPresetNumber, index, NUM_SWITCHES,
                                 favoritePresetModeEnabled, favoritePresets, PRESET_COUNT);
}
