#include "Controller.h"

HardwareSerial MidiSerial(2);
AxeSystem Axe(AxeSystem::FRACTAL_PRODUCT_FM3);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_MCP23X17 mcp;
EffectSlot slots[NUM_SWITCHES] = {
  {ID_COMP1, "CMP", 0}, {EffectId(118), "DRV", 1}, {ID_CHORUS1, "MOD", 2},
  {ID_DELAY1, "DLY", 3}, {ID_REVERB1, "REV", 4}, {ID_FILTER1, "BST", 5}
};
ControllerMode currentMode = MODE_EFFECTS;
PresetNumber currentPresetNumber = -1;
SceneNumber currentSceneNumber = -1;
char currentPresetName[NAME_SIZE] = "---", currentSceneName[NAME_SIZE] = "---";
char sceneNames[AxeSystem::MAX_SCENES][NAME_SIZE] = {};
QueueHandle_t commandQueue = nullptr;
bool displayDirty = true;
String lastOperation;

void setup() {
  Serial.begin(115200);
  commandQueue = xQueueCreate(16, sizeof(Command));
  if (!commandQueue) Serial.println("Befehlsqueue konnte nicht angelegt werden");
  loadPresetCache();
  loadFavoriteSettings();
  setupDisplays();
  setupHardwareButtons();
  setupMidi();
  setupWebServer();
}

void loop() {
  Axe.update();
  checkHardwareButtons();
  Command command;
  if (commandQueue && xQueueReceive(commandQueue, &command, 0) == pdTRUE) executeCommand(command);
  unsigned long now = millis();
  updatePresets(now);
  if (displayDirty) { displayDirty = false; drawAllSlots(true); }
  updateDisplays(now);
  updateNetwork(now);
  static unsigned long lastStatus = 0;
  if (now - lastStatus >= 250) {
    lastStatus = now;
    updateSlotStatesFromAxe();
    publishWebState();
  }
  delay(1);
}
