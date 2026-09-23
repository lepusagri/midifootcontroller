#include "Controller.h"

bool buttonsReady = false;
void setupHardwareButtons() {
  buttonsReady = mcp.begin_I2C(MCP_ADDR, &Wire);
  if (!buttonsReady) { Serial.println("MCP23017 nicht gefunden; Taster deaktiviert"); return; }
  for (auto& slot : slots) mcp.pinMode(slot.mcpPin, INPUT_PULLUP);
}
void toggleEffect(uint8_t index) {
  if (index >= NUM_SWITCHES || !presetReady || presetPending || !slots[index].available) return;
  Axe.toggleEffect(slots[index].effectId);
  updateSlotStatesFromAxe();
}
void executeCommand(const Command& command) {
  if (isScanning && command.type != CommandType::ScanStop && command.type != CommandType::Save) {
    lastOperation = "Scan laeuft; zum Bedienen zuerst stoppen";
    return;
  }
  switch (command.type) {
    case CommandType::Preset: selectPreset(command.value); break;
    case CommandType::PresetUp:
      if (validPreset(currentPresetNumber)) selectPreset((currentPresetNumber + 1) % PRESET_COUNT);
      break;
    case CommandType::PresetDown:
      if (validPreset(currentPresetNumber)) selectPreset((currentPresetNumber + LAST_PRESET) % PRESET_COUNT);
      break;
    case CommandType::Scene: selectScene(command.value); break;
    case CommandType::SceneUp: selectScene(currentSceneNumber + 1); break;
    case CommandType::SceneDown: selectScene(currentSceneNumber - 1); break;
    case CommandType::Effect: toggleEffect(command.value); break;
    case CommandType::ScanSmart: startScan(false); break;
    case CommandType::ScanDeep: startScan(true); break;
    case CommandType::ScanStop: stopScan(); lastOperation = "Scan gestoppt"; break;
    case CommandType::Save: savePresetCache(); break;
  }
}
void toggleSlot(uint8_t index) {
  if (index >= NUM_SWITCHES) return;
  if (currentMode == MODE_EFFECTS) executeCommand({CommandType::Effect, index});
  else if (currentMode == MODE_SCENES) executeCommand({CommandType::Scene, index + 1});
  else if (validPreset(currentPresetNumber)) executeCommand({CommandType::Preset, currentPresetNumber + int(index) - 2});
}
void checkHardwareButtons() {
  if (!buttonsReady) return;
  const unsigned long now = millis();
  const uint8_t pins = mcp.readGPIOA();
  for (uint8_t i = 0; i < NUM_SWITCHES; ++i) {
    auto& slot = slots[i];
    bool reading = (pins & (1 << slot.mcpPin)) != 0;
    if (reading != slot.lastButtonState) {
      slot.lastDebounceTime = now;
      slot.lastButtonState = reading;
    }
    if (now - slot.lastDebounceTime <= 50) continue;
    if (!reading && !slot.isPressed) {
      slot.isPressed = true;
      slot.pressStartTime = now;
      slot.holdExecuted = false;
    }
    if (!reading && slot.isPressed && !slot.holdExecuted && now - slot.pressStartTime >= HOLD_DURATION_MS) {
      if (i == 0 || i == 1) {
        currentMode = i == 0 ? (currentMode == MODE_EFFECTS ? MODE_SCENES : MODE_EFFECTS)
                            : (currentMode == MODE_PRESETS ? MODE_SCENES : MODE_PRESETS);
        if (currentMode == MODE_SCENES) requestAllSceneNames();
        scrollOffset = 0;
        displayDirty = true;
        slot.holdExecuted = true;
      } else if (i == 4 || i == 5) {
        executeCommand({i == 4 ? CommandType::PresetUp : CommandType::PresetDown, 0});
        slot.holdExecuted = true;
      }
    }
    if (reading && slot.isPressed) {
      slot.isPressed = false;
      if (!slot.holdExecuted) toggleSlot(i);
      slot.holdExecuted = false;
    }
  }
}
