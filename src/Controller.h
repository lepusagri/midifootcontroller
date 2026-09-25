#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MCP23X17.h>
#include <AxeFxControl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "CustomMidi.h"

constexpr int PRESET_COUNT = 501;
constexpr int LAST_PRESET = PRESET_COUNT - 1;
constexpr int NAME_SIZE = AxePreset::MAX_PRESET_NAME + 1;
constexpr uint8_t NUM_SWITCHES = 6;
constexpr int EMPTY_FAVORITE = -1;
constexpr int SCREEN_WIDTH = 128, SCREEN_HEIGHT = 64;
constexpr uint8_t TCA_ADDR = 0x70, MCP_ADDR = 0x20;
constexpr unsigned long HOLD_DURATION_MS = 1000;
constexpr unsigned long NETWORK_RESET_HOLD_MS = 10000;
enum ControllerMode { MODE_EFFECTS, MODE_SCENES, MODE_PRESETS, MODE_CUSTOM_MIDI };
enum class CacheState : uint8_t { Unknown, Known, Timeout };
enum class CommandType : uint8_t { Preset, PresetUp, PresetDown, Scene,
  SceneUp, SceneDown, Effect, ScanSmart, ScanDeep, ScanStop, Save,
  FavoriteToggle, FavoriteMove, FavoriteMode, CustomMidiTrigger,
  CustomMidiSetMode, CustomMidiSetName, CustomMidiSetCommand, CustomMidiRemoveCommand, CustomMidiSave };
struct Command { CommandType type; int value; int secondary = 0; int third = 0;
  int fourth = 0; int fifth = 0; int sixth = 0; int seventh = 0;
  char name[CUSTOM_MIDI_NAME_LENGTH + 1] = {};
  Command(CommandType t, int v, int s = 0, int a = 0, int b = 0, int c = 0, int d = 0, int e = 0)
      : type(t), value(v), secondary(s), third(a), fourth(b), fifth(c), sixth(d), seventh(e) {}
  Command() : type(CommandType::Save), value(0) {}
};
struct EffectSlot {
  EffectId effectId;
  const char* label;
  bool available = false, active = false;
  bool lastDrawnAvailable = false, lastDrawnActive = false;
  uint8_t mcpPin;
  bool lastButtonState = HIGH;
  unsigned long lastDebounceTime = 0;
  bool isPressed = false;
  unsigned long pressStartTime = 0;
  bool holdExecuted = false;
  EffectSlot(EffectId id, const char* text, uint8_t pin)
      : effectId(id), label(text), mcpPin(pin) {}
};
extern HardwareSerial MidiSerial;
extern AxeSystem Axe;
extern Adafruit_SSD1306 display;
extern Adafruit_MCP23X17 mcp;
extern EffectSlot slots[NUM_SWITCHES];
extern ControllerMode currentMode;
extern PresetNumber currentPresetNumber;
extern SceneNumber currentSceneNumber;
extern char currentPresetName[NAME_SIZE], currentSceneName[NAME_SIZE];
extern char sceneNames[AxeSystem::MAX_SCENES][NAME_SIZE];
extern char presetRamCache[PRESET_COUNT][NAME_SIZE];
extern CacheState presetCacheState[PRESET_COUNT];
extern bool cacheIsDirty, isScanning, scanDeepMode, buttonsReady, storageReady;
extern bool presetPending, presetReady;
extern bool displayDirty;
extern int scanCurrentPreset;
extern uint32_t cacheRevision;
extern int favoritePresets[NUM_SWITCHES];
extern bool favoritePresetModeEnabled;
extern uint32_t favoritesRevision;
extern size_t scrollOffset;
extern QueueHandle_t commandQueue;
extern String lastOperation;

bool validPreset(int number);
void copyName(char* target, const char* name);
void setupDisplays();
void drawAllSlots(bool force);
void drawSlot(uint8_t index, bool force);
void updateDisplays(unsigned long now);
void setupHardwareButtons();
void checkHardwareButtons();
void toggleSlot(uint8_t index);
void toggleEffect(uint8_t index);
void selectPreset(int number);
void selectScene(int number);
void setCurrentSceneLocal(SceneNumber scene);
void requestAllSceneNames();
void updateSlotStatesFromAxe();
void executeCommand(const Command& command);
void loadPresetCache();
void savePresetCache();
void loadFavoriteSettings();
void toggleFavorite(int number);
void moveFavorite(uint8_t slot, int direction);
void setFavoritePresetMode(bool enabled);
bool usesFavoritePresetMode();
int presetForSwitch(uint8_t index);
void startScan(bool deep);
void stopScan();
void updatePresets(unsigned long now);
void setupMidi();
void setupWebServer();
void setupNetwork();
void updateNetwork(unsigned long now);
bool networkIsAccessPoint();
String networkAddress();
String networkSettingsJson();
bool saveNetworkSettings(bool accessPoint, const String& ssid, const String& password,
                         bool dhcp, const String& address, const String& gateway,
                         const String& subnet, const String& dns, String& error);
void resetNetworkToAccessPoint();
void publishWebState();
