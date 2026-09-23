#include "Controller.h"
#include "ControllerLogic.h"

static bool displayReady[NUM_SWITCHES] = {};
static ControllerMode lastDrawnMode = MODE_EFFECTS;
static SceneNumber lastDrawnSceneNumber = 0;
static int lastDrawnPresetNumForSlot[NUM_SWITCHES] = {-99, -99, -99, -99, -99, -99};
static size_t lastDrawnScrollOffset[NUM_SWITCHES] = {99999, 99999, 99999, 99999, 99999, 99999};
size_t scrollOffset = 0;
static bool selectDisplay(uint8_t channel) {
  if (channel > 7) return false;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << channel);
  return Wire.endTransmission() == 0;
}

void drawCenteredText(const char* text, uint8_t textSize) {
  display.setTextSize(textSize);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
  display.print(text);
}

void drawPresetTicker(uint8_t index, int targetPresetNum, bool force = false) {
  if (!force && lastDrawnPresetNumForSlot[index] == targetPresetNum && lastDrawnScrollOffset[index] == scrollOffset) {
    return;
  }

  display.clearDisplay();
  String nameToDisplay = "";
  
  const bool isCurrent = targetPresetNum == currentPresetNumber;
  if (isCurrent) {
    nameToDisplay = String(currentPresetName);
  } else {
    if (targetPresetNum >= 0 && targetPresetNum <= 500) {
      nameToDisplay = String(presetRamCache[targetPresetNum]); 
    }
  }
  nameToDisplay.trim();

  if (isCurrent) {
    display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    
    display.setTextSize(2);
    String prNumStr = "PR " + String(targetPresetNum);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(prNumStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 6);
    display.print(prNumStr);
    
    if (nameToDisplay.length() > 0 && nameToDisplay != "---" && nameToDisplay != "[Leer/Timeout]") {
      uint16_t estimatedWidth = nameToDisplay.length() * 12;
      
      if (estimatedWidth <= SCREEN_WIDTH) {
        display.getTextBounds(nameToDisplay.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor((SCREEN_WIDTH - w) / 2, 38);
        display.print(nameToDisplay);
      } else {
        String scrolledText = nameToDisplay + "   " + nameToDisplay;
        size_t localOffset = scrollOffset % (nameToDisplay.length() + 3);
        String visiblePart = scrolledText.substring(localOffset, localOffset + 10);
        display.setCursor(4, 38);
        display.print(visiblePart);
      }
    } else {
      display.getTextBounds("[ LEER ]", 0, 0, &x1, &y1, &w, &h);
      display.setCursor((SCREEN_WIDTH - w) / 2, 38);
      display.print("[ LEER ]");
    }
  } 
  else {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(4, 4);
    display.print("PR " + String(targetPresetNum));
    
    int16_t x1, y1; uint16_t w, h;
    
    if (nameToDisplay.length() > 0 && nameToDisplay != "---" && nameToDisplay != "[Leer/Timeout]") {
      display.setTextSize(2);
      display.getTextBounds(nameToDisplay.c_str(), 0, 0, &x1, &y1, &w, &h);
      
      if (w <= SCREEN_WIDTH) {
        display.setCursor((SCREEN_WIDTH - w) / 2, 34);
        display.print(nameToDisplay);
      } else {
        String scrolledText = nameToDisplay + "   " + nameToDisplay;
        size_t localOffset = scrollOffset % (nameToDisplay.length() + 3);
        String visiblePart = scrolledText.substring(localOffset, localOffset + 10);
        display.setCursor(4, 34);
        display.print(visiblePart);
      }
    } else {
      display.setTextSize(2);
      display.getTextBounds("[ LEER ]", 0, 0, &x1, &y1, &w, &h);
      display.setCursor((SCREEN_WIDTH - w) / 2, 34);
      display.print("[ LEER ]");
    }
  }
  
  display.display(); 
  lastDrawnPresetNumForSlot[index] = targetPresetNum;
  lastDrawnScrollOffset[index] = scrollOffset;
}

void drawSlot(uint8_t index, bool force) {
  if (index >= NUM_SWITCHES || !displayReady[index]) return;
  EffectSlot& slot = slots[index];
  
  if (!selectDisplay(index)) return;

  if (currentMode == MODE_EFFECTS) {
    if (!force && lastDrawnMode == MODE_EFFECTS && slot.available == slot.lastDrawnAvailable && slot.active == slot.lastDrawnActive) return;
    
    display.clearDisplay();
    if (!slot.available) {
      display.setTextColor(SSD1306_WHITE); drawCenteredText("---", 3);
    } else if (slot.active) {
      display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK); drawCenteredText(slot.label, 4);
    } else {
      display.setTextColor(SSD1306_WHITE); drawCenteredText(slot.label, 4);
    }
    display.display();
    
    slot.lastDrawnAvailable = slot.available; 
    slot.lastDrawnActive = slot.active;
  } 
  else if (currentMode == MODE_SCENES) {
    uint8_t sceneNumForSlot = index + 1;
    bool isCurrentSceneActive = (currentSceneNumber == sceneNumForSlot);
    
    if (!force && lastDrawnMode == MODE_SCENES && lastDrawnSceneNumber == currentSceneNumber) return;

    display.clearDisplay();
    if (isCurrentSceneActive) {
      display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    const SceneLabel label = makeSceneLabel(sceneNames[index], sceneNumForSlot);
    if (label.lineCount == 1) {
      drawCenteredText(label.lines[0], label.textSize);
    } else {
      display.setTextSize(label.textSize);
      const int lineHeight = 8 * label.textSize;
      for (uint8_t line = 0; line < label.lineCount; ++line) {
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(label.lines[line], 0, 0, &x1, &y1, &w, &h);
        display.setCursor((SCREEN_WIDTH - w) / 2,
                          (SCREEN_HEIGHT - label.lineCount * lineHeight) / 2 + line * lineHeight);
        display.print(label.lines[line]);
      }
    }
    display.display();
  }
  else if (currentMode == MODE_PRESETS) {
    const int targetPresetNum = presetForSwitch(index);
    
    if (!validPreset(currentPresetNumber) || !validPreset(targetPresetNum)) {
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      drawCenteredText("---", 3);
      display.display();
    } else {
      drawPresetTicker(index, targetPresetNum, force);
    }
  }
}

void drawAllSlots(bool force) {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    drawSlot(i, force);
    Axe.update();
  }
  lastDrawnMode = currentMode;
  lastDrawnSceneNumber = currentSceneNumber;
}

void setupDisplays() {
  Wire.begin(21, 22);
  Wire.setClock(400000);
  display.setTextWrap(false);
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (!selectDisplay(i)) continue;
    Wire.beginTransmission(0x3C);
    bool ok = Wire.endTransmission() == 0 && display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
    displayReady[i] = ok;
    if (!ok) { Serial.printf("Display %u initialization failed\n", i); continue; }
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
    drawCenteredText("BOOT", 2);
    display.display();
  }
}


void updateDisplays(unsigned long now) {
  static unsigned long lastScrollTime = 0;
  if (currentMode != MODE_PRESETS || now - lastScrollTime < 350) return;
  lastScrollTime = now;
  ++scrollOffset;
  for (uint8_t i = 0; i < NUM_SWITCHES; ++i) {
    const int target = presetForSwitch(i);
    if (!displayReady[i] || !validPreset(target)) continue;
    const char* name = target == currentPresetNumber ? currentPresetName : presetRamCache[target];
    if (strlen(name) <= 10) continue; // Static labels need no periodic I2C transfer.
    if (!selectDisplay(i)) continue;
    drawPresetTicker(i, target);
    Axe.update(); // Drain incoming MIDI between display transfers.
  }
}
