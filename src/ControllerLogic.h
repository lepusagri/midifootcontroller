#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct SceneLabel {
  char lines[2][33] = {};
  uint8_t textSize = 2;
  uint8_t lineCount = 1;
};

// Default GFX font: 6 pixels per character. Keep every line inside 128 pixels.
inline SceneLabel makeSceneLabel(const char* name, unsigned sceneNumber) {
  SceneLabel label;
  size_t length = 0;
  if (name) while (length < 32 && name[length]) ++length;
  while (length && static_cast<unsigned char>(name[length - 1]) <= ' ') --length;
  size_t start = 0;
  while (start < length && static_cast<unsigned char>(name[start]) <= ' ') ++start;
  length -= start;
  if (!length || (length == 3 && memcmp(name + start, "---", 3) == 0)) {
    snprintf(label.lines[0], sizeof(label.lines[0]), "SCN %u", sceneNumber);
    label.textSize = 3;
    return label;
  }
  name += start;
  if (length <= 21) {
    memcpy(label.lines[0], name, length);
    label.textSize = length <= 10 ? 2 : 1;
    return label;
  }
  label.textSize = 1;
  label.lineCount = 2;
  size_t split = 21;
  // Prefer a word boundary, provided both lines still fit.
  for (size_t pos = 21; pos >= length - 21; --pos) {
    if (name[pos] == ' ') { split = pos; break; }
  }
  memcpy(label.lines[0], name, split);
  size_t next = split;
  while (next < length && name[next] == ' ') ++next;
  memcpy(label.lines[1], name + next, length - next);
  return label;
}

// These helpers have no Arduino dependencies and can be exercised on the host.
inline bool parseUnsigned(const char* text, int maximum, int& result) {
  if (!text || !*text) return false;
  int value = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    int digit = *p - '0';
    if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10)) return false;
    value = value * 10 + digit;
  }
  result = value;
  return true;
}
inline bool scanTimedOut(uint32_t now, uint32_t started, uint32_t timeout) {
  return uint32_t(now - started) >= timeout;
}
inline int favoritePresetForSwitch(int currentPreset, unsigned index, unsigned switchCount,
                                   bool favoriteModeEnabled, const int* favorites,
                                   int presetCount) {
  if (index >= switchCount) return -1;
  bool hasFavorite = false;
  if (favoriteModeEnabled && favorites) {
    for (unsigned slot = 0; slot < switchCount; ++slot) {
      if (favorites[slot] >= 0 && favorites[slot] < presetCount) { hasFavorite = true; break; }
    }
  }
  if (hasFavorite) {
    const int favorite = favorites[index];
    return favorite >= 0 && favorite < presetCount ? favorite : -1;
  }
  const int relative = currentPreset + static_cast<int>(index) - 2;
  return currentPreset >= 0 && currentPreset < presetCount && relative >= 0 && relative < presetCount
    ? relative : -1;
}
enum class ScanDecision { Wait, Received, Timeout };
inline ScanDecision scanDecision(bool received, uint32_t now, uint32_t started) {
  if (received && uint32_t(now - started) >= 500) return ScanDecision::Received;
  if (scanTimedOut(now, started, 1500)) return ScanDecision::Timeout;
  return ScanDecision::Wait;
}
template<class Text> void appendJsonString(Text& result, const char* value) {
  result += '"';
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
    if (*p == '"' || *p == '\\') { result += '\\'; result += char(*p); }
    else if (*p < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
      result += escaped;
    } else result += char(*p);
  }
  result += '"';
}
