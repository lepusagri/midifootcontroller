#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MCP23X17.h>
#include <LittleFS.h> 

// Hardware-Register einbinden, um den Brownout-Schutz abzuschalten
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ==================================================
// WLAN & Netzwerk
// ==================================================
const char* WIFI_SSID = "HASE24";
const char* WIFI_PASS = "tLPA9!fU";
const char* HOSTNAME  = "fm3-controller";

AsyncWebServer server(80);

// ==================================================
// ESP32 Pins & I2C Adressen
// ==================================================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define MIDI_TX_PIN  17
#define MIDI_RX_PIN  16

#define TCA_ADDR 0x70
#define MCP_ADDR 0x20

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

HardwareSerial MidiSerial(2);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MCP23X17 mcp;

// KORREKTUR: Festes Char-Array gegen RAM-Fragmentierung (ca. 16 KB fest blockiert)
char presetRamCache[501][32]; 
bool cacheIsDirty = false; 

#include "AxeFxControl.h"
AxeSystem Axe(AxeSystem::FRACTAL_PRODUCT_FM3);

// ==================================================
// Controller-Modi
// ==================================================
enum ControllerMode {
  MODE_EFFECTS,
  MODE_SCENES,
  MODE_PRESETS
};
ControllerMode currentMode = MODE_EFFECTS;

// ==================================================
// Preset-Scanner Variablen
// ==================================================
bool isScanning = false;
bool scanDeepMode = false; 
int scanCurrentPreset = 0;
unsigned long scanStartTime = 0;
const unsigned long SCAN_TIMEOUT_MS = 1500; 

// ==================================================
// Effekt-Slots & Taster-Struktur
// ==================================================
const uint8_t NUM_SWITCHES = 6;
const unsigned long HOLD_DURATION_MS = 1000; 

struct EffectSlot {
  EffectId effectId;
  const char* label;
  bool available;
  bool active;
  bool lastDrawnAvailable;
  bool lastDrawnActive;
  uint8_t mcpPin;
  bool lastButtonState;           
  unsigned long lastDebounceTime; 
  bool isPressed;                 
  unsigned long pressStartTime;   
  bool holdExecuted;              
};

EffectSlot slots[NUM_SWITCHES] = {
  { ID_COMP1,    "CMP", false, false, false, false, 0, HIGH, 0, false, 0, false },
  { (EffectId)118, "DRV", false, false, false, false, 1, HIGH, 0, false, 0, false }, 
  { ID_CHORUS1,  "MOD", false, false, false, false, 2, HIGH, 0, false, 0, false },
  { ID_DELAY1,   "DLY", false, false, false, false, 3, HIGH, 0, false, 0, false },
  { ID_REVERB1,  "REV", false, false, false, false, 4, HIGH, 0, false, 0, false },
  { ID_FILTER1,  "BST", false, false, false, false, 5, HIGH, 0, false, 0, false }
};

char currentPresetName[33] = "---";
char currentSceneName[33]  = "---";
char sceneNames[AxeSystem::MAX_SCENES][33];
PresetNumber currentPresetNumber = -1;
SceneNumber currentSceneNumber = 1;

unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL_MS = 250;

unsigned long lastScrollTime = 0;
const unsigned long SCROLL_INTERVAL_MS = 350; 
size_t scrollOffset = 0;

ControllerMode lastDrawnMode = MODE_EFFECTS;
SceneNumber lastDrawnSceneNumber = 0;
PresetNumber lastDrawnPresetNumber = -1;
int lastDrawnPresetNumForSlot[NUM_SWITCHES] = {-99, -99, -99, -99, -99, -99};
size_t lastDrawnScrollOffset[NUM_SWITCHES] = {99999, 99999, 99999, 99999, 99999, 99999};

void drawAllSlots(bool force);
void setCurrentSceneLocal(SceneNumber scene);
void requestAllSceneNames();

// ==================================================
// Display & TCA9548A Logik
// ==================================================
void selectDisplay(uint8_t channel) {
  if (channel > 7) return;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
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
  
  if (index == 2) {
    nameToDisplay = String(currentPresetName);
  } else {
    if (targetPresetNum >= 0 && targetPresetNum <= 500) {
      nameToDisplay = String(presetRamCache[targetPresetNum]); 
    }
  }
  nameToDisplay.trim();

  if (index == 2) {
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
  if (index >= NUM_SWITCHES) return;
  EffectSlot& slot = slots[index];
  
  selectDisplay(index);

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

    if (strlen(sceneNames[index]) > 0 && strcmp(sceneNames[index], "---") != 0) {
      drawCenteredText(sceneNames[index], 2);
    } else {
      String fallbackLabel = "SCN " + String(sceneNumForSlot);
      drawCenteredText(fallbackLabel.c_str(), 3);
    }
    display.display();
  }
  else if (currentMode == MODE_PRESETS) {
    int relativeOffset = (int)index - 2; 
    int targetPresetNum = (int)currentPresetNumber + relativeOffset;
    
    if (targetPresetNum < 0) {
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
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) drawSlot(i, force);
  lastDrawnMode = currentMode;
  lastDrawnSceneNumber = currentSceneNumber;
  lastDrawnPresetNumber = currentPresetNumber;
}

void setupDisplays() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    selectDisplay(i);
    bool ok = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
    if (ok) drawCenteredText("BOOT", 2);
    display.display();
  }
}

void setupHardwareButtons() {
  if (!mcp.begin_I2C(MCP_ADDR, &Wire)) {
    Serial.println("MCP23017 nicht gefunden!"); return;
  }
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    mcp.pinMode(slots[i].mcpPin, INPUT_PULLUP);
  }
}

void setCurrentPresetLocal(PresetNumber number) {
  currentPresetNumber = number;
  const char* name = (number >= 0 && number <= 500) ? presetRamCache[number] : "---";
  strncpy(currentPresetName, name, sizeof(currentPresetName) - 1);
  currentPresetName[sizeof(currentPresetName) - 1] = '\0';
}

void toggleSlot(uint8_t index) {
  if (index >= NUM_SWITCHES) return;
  
  if (currentMode == MODE_EFFECTS) {
    EffectSlot& slot = slots[index];
    if (!slot.available) return;
    Axe.toggleEffect(slot.effectId);
    slot.active = !slot.active;
    drawSlot(index, true);
  } 
  else if (currentMode == MODE_SCENES) {
    uint8_t targetScene = index + 1;
    if (targetScene <= AxeSystem::MAX_SCENES) {
      setCurrentSceneLocal(targetScene);
      Axe.sendSceneChange(targetScene);
      drawAllSlots(true);
    }
  }
  else if (currentMode == MODE_PRESETS) {
    int relativeOffset = (int)index - 2;
    int targetPresetNum = (int)currentPresetNumber + relativeOffset;
    if (targetPresetNum >= 0 && targetPresetNum <= 500) {
      scrollOffset = 0; 
      Axe.sendPresetChange(targetPresetNum);
      setCurrentPresetLocal(targetPresetNum);
      requestAllSceneNames();
      drawAllSlots(true);
    }
  }
}

void checkHardwareButtons() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    bool reading = mcp.digitalRead(slots[i].mcpPin);

    if (reading != slots[i].lastButtonState) {
      slots[i].lastDebounceTime = now;
      slots[i].lastButtonState = reading;
    }

    if ((now - slots[i].lastDebounceTime) > 50) { 
      
      if (reading == LOW && slots[i].isPressed == false) {
        slots[i].isPressed = true;
        slots[i].pressStartTime = now;
        slots[i].holdExecuted = false;
      }

      if (reading == LOW && slots[i].isPressed == true && !slots[i].holdExecuted) {
        if ((now - slots[i].pressStartTime) >= HOLD_DURATION_MS) {
          
          if (i == 0) { 
            if (currentMode == MODE_PRESETS || currentMode == MODE_SCENES) {
              currentMode = MODE_EFFECTS;
            } else {
              currentMode = MODE_SCENES;
            }
            slots[i].holdExecuted = true;
            if(currentMode == MODE_SCENES) requestAllSceneNames(); 
            drawAllSlots(true);
          }
          else if (i == 1) { 
            if (currentMode == MODE_PRESETS) {
              currentMode = MODE_SCENES;
              requestAllSceneNames();
            } else {
              currentMode = MODE_PRESETS;
              scrollOffset = 0;
            }
            slots[i].holdExecuted = true;
            drawAllSlots(true);
          }
          else if (i == 4) {
            Axe.sendPresetIncrement();
            currentSceneNumber = 1;
            setCurrentSceneLocal(1);
            Axe.requestPresetDetails();
            slots[i].holdExecuted = true;
            drawAllSlots(true);
          }
          else if (i == 5) {
            Axe.sendPresetDecrement();
            currentSceneNumber = 1;
            setCurrentSceneLocal(1);
            Axe.requestPresetDetails();
            slots[i].holdExecuted = true;
            drawAllSlots(true);
          }
          
        }
      }

      if (reading == HIGH && slots[i].isPressed == true) {
        slots[i].isPressed = false;
        if (slots[i].holdExecuted) {
          slots[i].holdExecuted = false; 
        } else {
          toggleSlot(i); 
        }
      }

    }
  }
}

void updateSlotStatesFromAxe() {
  AxePreset& preset = Axe.getCurrentPreset();
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    slots[i].available = preset.hasEffect(slots[i].effectId);
    slots[i].active = slots[i].available && Axe.isEffectEnabled(slots[i].effectId);
  }
  if (currentMode != MODE_PRESETS) {
    drawAllSlots(false);
  }
}

void setCurrentSceneLocal(SceneNumber scene) {
  if (scene < 1 || scene > AxeSystem::MAX_SCENES) return;
  currentSceneNumber = scene;
  strncpy(currentSceneName, sceneNames[scene - 1], sizeof(currentSceneName) - 1);
}

void requestAllSceneNames() {
  for (uint8_t i = 1; i <= AxeSystem::MAX_SCENES; i++) Axe.requestSceneName(i);
}

void onPresetNameChanged(const PresetNumber number, const char* name, const byte length) {
  currentPresetNumber = number; 
  strncpy(currentPresetName, name, sizeof(currentPresetName) - 1);
  currentPresetName[sizeof(currentPresetName) - 1] = '\0';
  
  if (number >= 0 && number <= 500) {
    if (strcmp(presetRamCache[number], currentPresetName) != 0) { 
      strncpy(presetRamCache[number], currentPresetName, 31);
      presetRamCache[number][31] = '\0'; // Korrektur: Array-Zuweisung korrigiert auf korrekte Indizierung zur Nullterminierung
      cacheIsDirty = true; 
    }
  }

  if (currentMode == MODE_PRESETS) drawAllSlots(true);
}

void onSceneNameChanged(const SceneNumber number, const char* name, const byte length) {
  if (number >= 1 && number <= AxeSystem::MAX_SCENES) {
    strncpy(sceneNames[number - 1], name, 32);
    if (currentMode == MODE_SCENES) drawAllSlots(true); 
  }
}

void onPresetChanged(AxePreset preset) { updateSlotStatesFromAxe(); requestAllSceneNames(); }
void onEffectsReceived(const PresetNumber number, AxePreset preset) { updateSlotStatesFromAxe(); }

String jsonStatus() {
  String json = "{";
  json += "\"scanning\":" + String(isScanning ? "true" : "false") + ",";
  json += "\"scanProgress\":" + String(scanCurrentPreset) + ",";
  json += "\"unsaved\":" + String(cacheIsDirty ? "true" : "false") + ",";
  json += "\"presetNumber\":" + String(currentPresetNumber) + ",";
  json += "\"presetName\":\"" + String(currentPresetName) + "\",";
  json += "\"sceneNumber\":" + String(currentSceneNumber) + ",";
  json += "\"sceneName\":\"" + String(currentSceneName) + "\",";
  
  String modeStr = "effects";
  if (currentMode == MODE_SCENES) modeStr = "scenes";
  else if (currentMode == MODE_PRESETS) modeStr = "presets";
  json += "\"mode\":\"" + modeStr + "\",";
  
  json += "\"scenes\":[";
  for (uint8_t i = 0; i < AxeSystem::MAX_SCENES; i++) {
    if (i > 0) json += ",";
    json += "{\"number\":" + String(i + 1) + ",\"name\":\"" + String(sceneNames[i]) + "\",\"active\":" + String((currentSceneNumber == (i + 1)) ? "true" : "false") + "}";
  }
  json += "],\"slots\":[";
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (i > 0) json += ",";
    json += "{\"index\":" + String(i) + ",\"label\":\"" + String(slots[i].label) + "\",\"available\":" + String(slots[i].available ? "true" : "false") + ",\"active\":" + String(slots[i].active ? "true" : "false") + "}";
  }
  
  json += "],\"presetCache\":[";
  for (int i = 0; i <= 500; i++) {
    if (i > 0) json += ",";
    String pName = String(presetRamCache[i]); 
    pName.trim();
    if (pName.length() == 0 || pName == "---") pName = "[ LEER ]";
    pName.replace("\"", "\\\""); 
    json += "\"" + pName + "\"";
  }
  
  json += "]}";
  return json;
}

String htmlPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>FM3 Controller</title>
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #111; color: white; text-align: center; margin: 0; padding: 15px; touch-action: manipulation; }
    h2 { margin: 0 0 15px 0; font-weight: 400; letter-spacing: 1px; color: #fff; font-size: 22px; }
    .info { max-width: 560px; margin: 0 auto 15px auto; padding: 14px; background: #1c1c1c; border: 1px solid #333; border-radius: 12px; text-align: left; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    .info-row { display: flex; justify-content: space-between; gap: 12px; margin: 4px 0; font-size: 15px; }
    .label { color: #888; font-weight: 500; }
    .value { font-weight: bold; text-align: right; color: #00ffcc; }
    
    .preset-list-container { max-width: 560px; margin: 0 auto 15px auto; background: #1c1c1c; border: 1px solid #444; border-radius: 12px; padding: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    .preset-list-title { font-size: 12px; color: #aaa; text-transform: uppercase; margin-bottom: 8px; font-weight: 500; letter-spacing: 0.5px; text-align: left; padding-left: 4px; }
    .preset-select { width: 100%; background: #222; color: #fff; border: 1px solid #444; border-radius: 8px; padding: 4px; font-family: monospace; font-size: 14px; outline: none; box-sizing: border-box; }
    .preset-select option { padding: 6px 10px; border-bottom: 1px solid #2d2d2d; color: #ccc; }
    .preset-select option:selected { background: #00ffcc !important; color: black !important; font-weight: bold; }
    .preset-select option:checked { background: #00ffcc !important; color: black !important; }

    .preset-selector { max-width: 560px; margin: 0 auto 15px auto; background: #1c1c1c; border: 1px solid #444; border-radius: 12px; padding: 12px; }
    .picker-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-bottom: 12px; }
    .picker-col { display: flex; flex-direction: column; align-items: center; background: #252525; padding: 6px; border-radius: 8px; border: 1px solid #333; }
    .picker-col span { font-size: 11px; color: #aaa; text-transform: uppercase; margin-bottom: 2px; }
    .picker-col .digit { font-size: 32px; font-weight: bold; color: #fff; margin: 4px 0; font-family: monospace; }
    .btn-step { width: 100%; font-size: 22px; padding: 12px 0; background: #333; color: white; border: 1px solid #555; border-radius: 6px; cursor: pointer; }
    .btn-step:active { background: #555; }
    .btn-go { width: 100%; font-size: 18px; padding: 14px; background: #00ffcc; color: black; border: none; border-radius: 8px; font-weight: bold; cursor: pointer; letter-spacing: 1px; box-shadow: 0 4px 10px rgba(0,255,204,0.2); }
    .btn-go:active { transform: scale(0.98); background: #00cc99; }

    .navgrid, .scenegrid, .grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; max-width: 560px; margin: 0 auto 15px auto; }
    button { font-family: inherit; font-weight: bold; border-radius: 10px; cursor: pointer; transition: all 0.2s ease; border: 2px solid #444; }
    button:active { transform: scale(0.98); }
    .navgrid button { font-size: 16px; padding: 14px 6px; background: #252525; color: white; border-color: #555; }
    .scenegrid button { font-size: 14px; padding: 12px 6px; }
    .grid button { font-size: 24px; padding: 22px 6px; border-radius: 12px; }
    button.on { background: #00ffcc; color: black; border-color: #00ffcc; box-shadow: 0 0 12px rgba(0,255,204,0.3); }
    button.off { background: #2a2a2a; color: white; }
    button.unavailable { background: #141414; color: #444; border-color: #222; cursor: not-allowed; pointer-events: none; }
    .status { margin-top: 15px; color: #666; font-size: 12px; font-family: monospace; }
  </style>
</head>
<body>
  <h2>FM3 Remote Interface</h2>
  <button id="save-flash-btn" onclick="saveToFlash()" style="width:100%; max-width:560px; margin: 0 auto 15px auto; padding: 10px; font-size: 14px; background: #222; color: #aaa; border: 1px solid #444; display: block;">Namen gespeichert</button>

  <div style="display: flex; gap: 10px; max-width: 560px; margin: 0 auto 15px auto;">
    <button id="scan-smart-btn" onclick="startScan('smart')" style="flex: 1; padding: 12px 6px; font-size: 13px; background: #222; color: #fff; border: 1px solid #444;">Zap-Scan (Nur leere)</button>
    <button id="scan-deep-btn" onclick="startScan('deep')" style="flex: 1; padding: 12px 6px; font-size: 13px; background: #222; color: #ff5500; border: 1px solid #444;">Deep-Scan (Alle 501)</button>
    <button id="scan-stop-btn" onclick="stopScan()" style="display: none; flex: 1; padding: 12px 6px; font-size: 13px; background: #cc0000; color: white; border: 1px solid #cc0000;">🛑 Scan Stoppen</button>
  </div>

  <div class="info">
    <div class="info-row"><div class="label">Preset</div><div class="value" id="preset">Loading...</div></div>
    <div class="info-row"><div class="label">Active Scene</div><div class="value" id="scene">Loading...</div></div>
  </div>

  <div class="preset-list-container">
    <div class="preset-list-title">Preset Schnellwahl (0 - 500)</div>
    <select id="preset-list-select" class="preset-select" size="10" onchange="selectBoxChanged(this.value)">
    </select>
  </div>

  <div class="preset-selector">
    <div class="picker-grid">
      <div class="picker-col">
        <span>Hunderter</span>
        <button class="btn-step" onclick="changeDigit(100, 1)">+</button>
        <div class="digit" id="d-hund">0</div>
        <button class="btn-step" onclick="changeDigit(100, -1)">&minus;</button>
      </div>
      <div class="picker-col">
        <span>Zehner</span>
        <button class="btn-step" onclick="changeDigit(10, 1)">+</button>
        <div class="digit" id="d-zehn">0</div>
        <button class="btn-step" onclick="changeDigit(10, -1)">&minus;</button>
      </div>
      <div class="picker-col">
        <span>Einer</span>
        <button class="btn-step" onclick="changeDigit(1, 1)">+</button>
        <div class="digit" id="d-ein">0</div>
        <button class="btn-step" onclick="changeDigit(1, -1)">&minus;</button>
      </div>
    </div>
    <button class="btn-go" id="go-btn" onclick="submitPreset()">GO TO PRESET</button>
  </div>

  <div class="navgrid">
    <button onclick="sendCmd('preset_down')">PRESET &minus;</button>
    <button onclick="sendCmd('preset_up')">PRESET &plus;</button>
    <button onclick="sendCmd('scene_down')">SCENE &minus;</button>
    <button onclick="sendCmd('scene_up')">SCENE &plus;</button>
  </div>
  <div class="scenegrid" id="scenegrid"></div>
  <div class="grid" id="grid"></div>
  <div class="status" id="status">Connecting to ESP32...</div>

  <script>
    let busy = false;
    let targetPresetValue = 0; 
    let userIsEditing = false; 
    let editTimeout;
    let selectListBuilt = false;

    function updatePickerUI() {
      document.getElementById('d-hund').textContent = Math.floor(targetPresetValue / 100);
      document.getElementById('d-zehn').textContent = Math.floor((targetPresetValue % 100) / 10);
      document.getElementById('d-ein').textContent = targetPresetValue % 10;
      
      const goBtn = document.getElementById('go-btn');
      if (userIsEditing) {
        goBtn.style.background = '#ffcc00';
        goBtn.textContent = 'GO TO PRESET ' + targetPresetValue;
      } else {
        goBtn.style.background = '#00ffcc';
        goBtn.textContent = 'GO TO PRESET';
      }
    }

    function changeDigit(weight, direction) {
      userIsEditing = true;
      clearTimeout(editTimeout);
      
      editTimeout = setTimeout(() => { 
        userIsEditing = false; 
        loadStatus(); 
      }, 10000);

      let temp = targetPresetValue + (weight * direction);
      if (temp >= 0 && temp <= 500) {
        targetPresetValue = temp;
        updatePickerUI();
        
        const sel = document.getElementById('preset-list-select');
        if(sel) {
          sel.value = targetPresetValue;
          scrollToActiveOption(sel);
        }
      }
    }

    async function selectBoxChanged(val) {
      const presetNum = parseInt(val);
      targetPresetValue = presetNum;
      userIsEditing = false;
      clearTimeout(editTimeout);
      updatePickerUI();
      
      await executePresetChange(presetNum);
    }

    async function executePresetChange(presetNum) {
      busy = true;
      document.getElementById('go-btn').style.background = '#333';
      document.getElementById('go-btn').style.color = '#fff';
      document.getElementById('go-btn').textContent = 'WIRD GELADEN...';
      
      try {
        document.getElementById('status').textContent = 'Sende Preset ' + presetNum + '...';
        await fetch('/api/scene?number=1'); 
        await fetch('/api/toggle?slot=' + (presetNum + 1000)); 
        busy = false;
        setTimeout(() => {
          document.getElementById('go-btn').style.color = 'black';
          loadStatus();
        }, 300);
      } catch (e) { busy = false; }
    }

    async function submitPreset() {
      userIsEditing = false;
      clearTimeout(editTimeout);
      await executePresetChange(targetPresetValue);
    }

    function scrollToActiveOption(selectElement) {
      const selectedOption = selectElement.options[selectElement.selectedIndex];
      if (selectedOption) {
        const optionHeight = selectedOption.offsetHeight || 28;
        selectElement.scrollTop = selectedOption.offsetTop - (optionHeight * 4.5);
      }
    }

    async function loadStatus() {
      if (busy) return;
      try {
        const r = await fetch('/api/status');
        const d = await r.json();
        document.getElementById('preset').textContent = d.presetNumber >= 0 ? d.presetNumber + ' - ' + d.presetName : d.presetName;
        document.getElementById('scene').textContent = d.sceneNumber >= 0 ? d.sceneNumber + ' - ' + d.sceneName : d.sceneName;
        
        const sel = document.getElementById('preset-list-select');
        if (sel && ( !selectListBuilt || d.scanning )) {
          const currentScroll = sel.scrollTop;
          const currentFocused = sel.value;
          
          sel.innerHTML = '';
          d.presetCache.forEach((name, idx) => {
            const opt = document.createElement('option');
            opt.value = idx;
            const paddedIdx = String(idx).padStart(3, '0');
            opt.textContent = paddedIdx + ' : ' + name;
            sel.appendChild(opt);
          });
          
          selectListBuilt = true;
          if(d.scanning) {
             sel.scrollTop = currentScroll;
             sel.value = currentFocused;
          }
        }

        if(!userIsEditing && !d.scanning) {
          if(d.presetNumber >= 0 && d.presetNumber <= 500) {
            targetPresetValue = d.presetNumber;
            updatePickerUI();
            
            if (sel && sel.value != d.presetNumber) {
              sel.value = d.presetNumber;
              scrollToActiveOption(sel);
            }
          }
        }

        const sg = document.getElementById('scenegrid');
        sg.innerHTML = '';
        d.scenes.forEach(s => {
          const b = document.createElement('button');
          b.textContent = s.number + ' - ' + s.name;
          b.className = s.active ? 'on' : 'off';
          b.onclick = () => setSc(s.number);
          sg.appendChild(b);
        });
        const g = document.getElementById('grid');
        g.innerHTML = '';
        d.slots.forEach(s => {
          const b = document.createElement('button');
          if (!s.available) {
            b.textContent = '---';
            b.className = 'unavailable';
            b.disabled = true;
          } else {
            b.textContent = s.label;
            b.className = s.active ? 'on' : 'off';
            b.onclick = () => tog(s.index);
          }
          g.appendChild(b);
        });
        document.getElementById('status').textContent = 'Sync Ok: ' + new Date().toLocaleTimeString();
        
        const sBtn = document.getElementById('save-flash-btn');
        if (d.unsaved) {
          sBtn.style.background = '#ff5500';
          sBtn.style.color = 'white';
          sBtn.style.borderColor = '#ff5500';
          sBtn.textContent = '⚠️ HIER KLICKEN: Neue Preset-Namen dauerhaft speichern!';
        } else {
          sBtn.style.background = '#222';
          sBtn.style.color = '#888';
          sBtn.style.borderColor = '#333';
          sBtn.textContent = '✓ Alle Namen dauerhaft im Speicher gesichert';
        }

        const smartBtn = document.getElementById('scan-smart-btn');
        const deepBtn = document.getElementById('scan-deep-btn');
        const stopBtn = document.getElementById('scan-stop-btn');

        if (d.scanning) {
          smartBtn.style.display = 'none';
          deepBtn.style.display = 'none';
          stopBtn.style.display = 'block';
          stopBtn.textContent = '🛑 Stoppen... (' + d.scanProgress + ' / 500)';
        } else {
          smartBtn.style.display = 'block';
          deepBtn.style.display = 'block';
          stopBtn.style.display = 'none';
        }

        const goBtn = document.getElementById('go-btn');
        if (d.scanning) {
          goBtn.disabled = true;
          goBtn.style.background = '#444';
          goBtn.style.color = '#888';
          goBtn.textContent = 'SCAN LÄUFT...';
        } else if (!userIsEditing) {
          goBtn.disabled = false;
          goBtn.style.background = '#00ffcc';
          goBtn.style.color = 'black';
        }

      } catch (e) { 
        document.getElementById('status').textContent = 'Connection lost. Retrying...'; 
      }
    }
    async function tog(i) {
      busy = true;
      try {
        await fetch('/api/toggle?slot=' + i);
        busy = false;
        await loadStatus();
      } catch (e) { busy = false; }
    }
    async function setSc(n) {
      busy = true;
      try {
        await fetch('/api/scene?number=' + n);
        busy = false;
        setTimeout(loadStatus, 150);
      } catch (e) { busy = false; }
    }
    async function sendCmd(c) {
      busy = true;
      try {
        document.getElementById('status').textContent = 'Sending command...';
        await fetch('/api/command?cmd=' + c);
        busy = false;
        setTimeout(loadStatus, 200);
      } catch (e) { busy = false; }
    }
    async function saveToFlash() {
      document.getElementById('save-flash-btn').textContent = 'Speichere im Flash...';
      try {
        await fetch('/api/save');
        loadStatus();
      } catch(e) { ; }
    }
    async function startScan(type) {
      let msg = type === 'deep' 
        ? "Deep Scan: Möchtest du ALLE 501 Presets komplett neu einlesen? Bestehende Namen werden überschrieben (Dauer: ca. 1-2 Min)."
        : "Smart Scan: Es werden nur Presets geladen, die noch keinen Namen haben. Bereits bekannte Presets werden übersprungen.";
        
      if (confirm(msg)) {
        await fetch('/api/scan/start?mode=' + type);
        loadStatus();
      }
    }
    async function stopScan() {
      await fetch('/api/scan/stop');
      loadStatus();
    }

    loadStatus();
    setInterval(loadStatus, 500);
  </script>
</body>
</html>
)rawliteral";
}

void handleCommand(const String& cmd) {
  if (cmd == "preset_down") { Axe.sendPresetDecrement(); currentSceneNumber = 1; setCurrentSceneLocal(1); requestAllSceneNames(); }
  else if (cmd == "preset_up") { Axe.sendPresetIncrement(); currentSceneNumber = 1; setCurrentSceneLocal(1); requestAllSceneNames(); }
  else if (cmd == "scene_down") { if (currentSceneNumber > 1) { setCurrentSceneLocal(currentSceneNumber - 1); Axe.sendSceneChange(currentSceneNumber); } }
  else if (cmd == "scene_up") { if (currentSceneNumber < AxeSystem::MAX_SCENES) { setCurrentSceneLocal(currentSceneNumber + 1); Axe.sendSceneChange(currentSceneNumber); } }
}

void setupWebServer() {
  Serial.println("Verbinde mit WLAN...");
  WiFi.mode(WIFI_STA); 
  WiFi.setHostname(HOSTNAME); 
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // WICHTIG: Warten, bis die Verbindung tatsächlich steht!
  int timeoutCounter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    timeoutCounter++;
    if (timeoutCounter > 30) { // Nach 15 Sekunden abbrechen, falls Router offline
      Serial.println("\nWLAN-Verbindung fehlgeschlagen! Starte Webserver nicht.");
      return;
    }
  }
  
  Serial.println("\nWLAN erfolgreich verbunden!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP()); // Zeigt dir die IP im Seriellen Monitor

  // Routen definieren
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) { 
    r->send(200, "text/html", htmlPage()); 
  });
  
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *r) { 
    r->send(200, "application/json", jsonStatus()); 
  });

  server.on("/api/save", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (cacheIsDirty) {
      File file = LittleFS.open("/presets.txt", FILE_WRITE);
      if (file) {
        for (int i = 0; i <= 500; i++) {
          file.println(String(presetRamCache[i])); 
        }
        file.close();
        cacheIsDirty = false; 
        Serial.println("Presets erfolgreich in LittleFS geschrieben.");
      } else {
        Serial.println("Fehler beim Öffnen der Datei zum Schreiben!");
      }
    }
    r->send(200, "application/json", jsonStatus());
  });

  server.on("/api/scan/start", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (!isScanning) {
      isScanning = true;
      scanCurrentPreset = 0;
      if (r->hasParam("mode") && r->getParam("mode")->value() == "deep") {
        scanDeepMode = true;
      } else {
        scanDeepMode = false;
      }
      Axe.sendPresetChange(scanCurrentPreset);
      scanStartTime = millis();
    }
    r->send(200, "application/json", jsonStatus());
  });

  server.on("/api/scan/stop", HTTP_GET, [](AsyncWebServerRequest *r) {
    isScanning = false;
    r->send(200, "application/json", jsonStatus());
  });

  server.on("/api/toggle", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (isScanning) {
      r->send(200, "application/json", jsonStatus());
      return;
    }

    int s = r->getParam("slot")->value().toInt(); 
    
    if (s >= 1000) {
      int directPreset = s - 1000;
      if (directPreset >= 0 && directPreset <= 500) {
        scrollOffset = 0;
        Axe.sendPresetChange(directPreset);
        setCurrentPresetLocal(directPreset);
        currentSceneNumber = 1; 
        setCurrentSceneLocal(1);
        Axe.requestPresetDetails();
        requestAllSceneNames();
        drawAllSlots(true);
      }
    } else if (s >= 0 && s < NUM_SWITCHES) {
      toggleSlot(s);
    }
    r->send(200, "application/json", jsonStatus());
  });

  server.on("/api/scene", HTTP_GET, [](AsyncWebServerRequest *r) {
    int s = r->getParam("number")->value().toInt(); 
    if (s >= 1 && s <= AxeSystem::MAX_SCENES) { 
      setCurrentSceneLocal(s); 
      Axe.sendSceneChange(s); 
    }
    r->send(200, "application/json", jsonStatus());
  });

  server.on("/api/command", HTTP_GET, [](AsyncWebServerRequest *r) {
    String c = r->getParam("cmd")->value(); 
    handleCommand(c); 
    r->send(200, "application/json", jsonStatus());
  });

  // Jetzt, wo Netzwerk & RAM bereit sind, den Server starten
  Serial.println("Starte Webserver HTTP-Listener...");
  server.begin();
  Serial.println("Webserver läuft bereit!");
}


void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  Serial.begin(115200); 
  delay(200);
  
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount-Fehler!");
  } else {
    Serial.println("LittleFS erfolgreich gemountet.");
  }
  
  if (LittleFS.exists("/presets.txt")) {
    File file = LittleFS.open("/presets.txt", FILE_READ);
    if (file) {
      int i = 0;
      while (file.available() && i <= 500) {
        String line = file.readStringUntil('\n');
        line.trim(); 
        
        strncpy(presetRamCache[i], line.c_str(), 31);
        presetRamCache[i][31] = '\0';
        i++;
      }
      file.close();
      Serial.println("Presets erfolgreich aus LittleFS geladen.");
    }
  } else {
    Serial.println("Keine presets.txt gefunden. Initialisiere leeren Cache.");
    for (int i = 0; i <= 500; i++) {
      presetRamCache[i][0] = '\0';
    }
  }
  
  setupDisplays();
  setupHardwareButtons();

  MidiSerial.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);
  Axe.begin(MidiSerial); Axe.fetchEffects(true);

  Axe.registerPresetNameCallback(onPresetNameChanged);
  Axe.registerSceneNameCallback(onSceneNameChanged);
  Axe.registerPresetChangeCallback(onPresetChanged);
  Axe.registerEffectsReceivedCallback(onEffectsReceived);

  Axe.requestPresetDetails(); requestAllSceneNames();
  drawAllSlots(true);

  delay(1000); 
  
  Serial.println("Starte Webserver...");
  setupWebServer();
}

void loop() {
  Axe.update();
  checkHardwareButtons();

  if (isScanning) {
    if (!scanDeepMode && scanCurrentPreset <= 500) {
      bool needToSkip = false;
      
      while (scanCurrentPreset <= 500 && 
             strlen(presetRamCache[scanCurrentPreset]) > 0 && 
             strcmp(presetRamCache[scanCurrentPreset], "---") != 0 && 
             strcmp(presetRamCache[scanCurrentPreset], "[Leer/Timeout]") != 0) {
        scanCurrentPreset++;
        needToSkip = true;
      }
      
      if (scanCurrentPreset > 500) {
        isScanning = false;
        Serial.println("Smart Scan beendet.");
      } else if (needToSkip) {
        Axe.sendPresetChange(scanCurrentPreset);
        scanStartTime = millis();
      }
    }

    if (isScanning) {
      bool readyForNext = false;

      if (currentPresetNumber == scanCurrentPreset && (millis() - scanStartTime > 500)) {
        if (strlen(presetRamCache[scanCurrentPreset]) > 0 && strcmp(presetRamCache[scanCurrentPreset], "---") != 0) {
          readyForNext = true;
        }
      }
      else if (millis() - scanStartTime >= SCAN_TIMEOUT_MS) {
        Serial.print("Timeout bei Preset "); Serial.println(scanCurrentPreset);
        strncpy(presetRamCache[scanCurrentPreset], "[Leer/Timeout]", 31);
        presetRamCache[scanCurrentPreset][31] = '\0';
        cacheIsDirty = true;
        readyForNext = true;
      }

      if (readyForNext) {
        scanCurrentPreset++;
        if (scanCurrentPreset > 500) {
          isScanning = false;
          Serial.println("Scan erfolgreich beendet!");
          drawAllSlots(true);
        } else {
          Axe.sendPresetChange(scanCurrentPreset);
          scanStartTime = millis(); 
        }
      }
    }
  }

  static bool mdnsStarted = false;
  if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); mdnsStarted = true; }
  }

  unsigned long now = millis();
  
  if (currentMode == MODE_PRESETS && (now - lastScrollTime >= SCROLL_INTERVAL_MS)) {
    lastScrollTime = now;
    scrollOffset++;
    
    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
      int relativeOffset = (int)i - 2;
      int targetPresetNum = (int)currentPresetNumber + relativeOffset;
      if (targetPresetNum >= 0 && targetPresetNum <= 500) {
        selectDisplay(i);
        drawPresetTicker(i, targetPresetNum);
      }
    }
  }

  if (now - lastStatusUpdate > STATUS_UPDATE_INTERVAL_MS) {
    lastStatusUpdate = now;
    updateSlotStatesFromAxe();
  }
}
