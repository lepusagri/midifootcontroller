#include "Controller.h"
#include <Preferences.h>
#include <string.h>

#define CUSTOM_MIDI_MAGIC 0x4C434D49u
#define CUSTOM_MIDI_VERSION 1u
#define CUSTOM_MIDI_STORED_COMMANDS 8u

typedef struct {
  uint8_t uiType;
  uint8_t uiChannel;
  uint8_t uiNumber;
  uint8_t uiValue;
} T_STORED_MIDI_COMMAND;

typedef struct {
  uint8_t uiMode;
  uint8_t uiCount[CUSTOM_MIDI_BANKS];
  T_STORED_MIDI_COMMAND tCommands[CUSTOM_MIDI_BANKS][CUSTOM_MIDI_STORED_COMMANDS];
} T_STORED_MIDI_SWITCH;

typedef struct {
  uint32_t uiMagic;
  uint8_t uiVersion;
  T_STORED_MIDI_SWITCH tSwitches[NUM_SWITCHES];
} T_STORED_MIDI_SETTINGS;

CustomMidiSwitch customMidiSwitches[NUM_SWITCHES];
uint32_t customMidiRevision = 1;
bool customMidiDirty = false;

//=================================================================================================
// Function     : saveCustomMidiSettings
// Purpose      : Store the complete Custom MIDI configuration in ESP32 preferences.
// Return Value : bool
//=================================================================================================
static bool saveCustomMidiSettings() {
  // +++++++++++++++++++++++++++++++++
  T_STORED_MIDI_SETTINGS tStored = {};
  Preferences tPreferences;
  bool bSaved = false;
  // +++++++++++++++++++++++++++++++++
  tStored.uiMagic = CUSTOM_MIDI_MAGIC;
  tStored.uiVersion = CUSTOM_MIDI_VERSION;
  for (uint8_t uiSlot = 0; uiSlot < NUM_SWITCHES; ++uiSlot) {
    const CustomMidiSwitch& tSource = customMidiSwitches[uiSlot];
    T_STORED_MIDI_SWITCH& tTarget = tStored.tSwitches[uiSlot];
    tTarget.uiMode = static_cast<uint8_t>(tSource.mode);
    for (uint8_t uiBank = 0; uiBank < CUSTOM_MIDI_BANKS; ++uiBank) {
      tTarget.uiCount[uiBank] = tSource.count[uiBank];
      for (uint8_t uiIndex = 0; uiIndex < tSource.count[uiBank]; ++uiIndex) {
        const CustomMidiCommand& tCommand = tSource.commands[uiBank][uiIndex];
        tTarget.tCommands[uiBank][uiIndex] = {
          static_cast<uint8_t>(tCommand.type), tCommand.channel, tCommand.number, tCommand.value
        };
      }
    }
  }
  if (!tPreferences.begin("lepus-custom", false)) return false;
  bSaved = tPreferences.putBytes("settings", &tStored, sizeof(tStored)) == sizeof(tStored);
  tPreferences.end();
  return bSaved;
}

//=================================================================================================
// Function     : markCustomMidiChanged
// Purpose      : Activate a configuration change in RAM for footswitch testing.
// Return Value : void
//=================================================================================================
static void markCustomMidiChanged(const char* psMessage) {
  customMidiDirty = true;
  ++customMidiRevision;
  displayDirty = true;
  lastOperation = psMessage;
}

//=================================================================================================
// Function     : loadCustomMidiSettings
// Purpose      : Load only a valid, compatible Custom MIDI configuration.
// Return Value : void
//=================================================================================================
void loadCustomMidiSettings() {
  // +++++++++++++++++++++++++++++++++
  T_STORED_MIDI_SETTINGS tStored = {};
  Preferences tPreferences;
  bool bLoaded = false;
  // +++++++++++++++++++++++++++++++++
  if (!tPreferences.begin("lepus-custom", true)) return;
  bLoaded = tPreferences.getBytesLength("settings") == sizeof(tStored) &&
            tPreferences.getBytes("settings", &tStored, sizeof(tStored)) == sizeof(tStored);
  tPreferences.end();
  if (!bLoaded || tStored.uiMagic != CUSTOM_MIDI_MAGIC || tStored.uiVersion != CUSTOM_MIDI_VERSION) return;
  for (uint8_t uiSlot = 0; uiSlot < NUM_SWITCHES; ++uiSlot) {
    const T_STORED_MIDI_SWITCH& tSwitch = tStored.tSwitches[uiSlot];
    if (tSwitch.uiMode > static_cast<uint8_t>(CustomMidiMode::Alternate)) return;
    for (uint8_t uiBank = 0; uiBank < CUSTOM_MIDI_BANKS; ++uiBank) {
      if (tSwitch.uiCount[uiBank] > CUSTOM_MIDI_STORED_COMMANDS) return;
      for (uint8_t uiIndex = 0; uiIndex < tSwitch.uiCount[uiBank]; ++uiIndex) {
        const T_STORED_MIDI_COMMAND& tCommand = tSwitch.tCommands[uiBank][uiIndex];
        if (tCommand.uiType != static_cast<uint8_t>(CustomMidiCommandType::ControlChange) ||
            tCommand.uiChannel < 1 || tCommand.uiChannel > 16 ||
            tCommand.uiNumber > 127 || tCommand.uiValue > 127) return;
      }
    }
  }
  for (uint8_t uiSlot = 0; uiSlot < NUM_SWITCHES; ++uiSlot) {
    CustomMidiSwitch& tTarget = customMidiSwitches[uiSlot];
    const T_STORED_MIDI_SWITCH& tSource = tStored.tSwitches[uiSlot];
    tTarget.mode = static_cast<CustomMidiMode>(tSource.uiMode);
    for (uint8_t uiBank = 0; uiBank < CUSTOM_MIDI_BANKS; ++uiBank) {
      // Older configurations may contain several commands; retain the first one.
      tTarget.count[uiBank] = tSource.uiCount[uiBank] ? 1 : 0;
      for (uint8_t uiIndex = 0; uiIndex < tTarget.count[uiBank]; ++uiIndex) {
        const T_STORED_MIDI_COMMAND& tCommand = tSource.tCommands[uiBank][uiIndex];
        tTarget.commands[uiBank][uiIndex] = {
          CustomMidiCommandType::ControlChange, tCommand.uiChannel, tCommand.uiNumber, tCommand.uiValue
        };
      }
    }
  }
}

//=================================================================================================
// Function     : triggerCustomMidi
// Purpose      : Send the selected bank once on a short footswitch release.
// Return Value : void
//=================================================================================================
void triggerCustomMidi(uint8_t uiSlot) {
  if (uiSlot >= NUM_SWITCHES) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[uiSlot];
  const uint8_t uiBank = tSwitch.mode == CustomMidiMode::Alternate ? tSwitch.nextBank : 0;
  for (uint8_t uiIndex = 0; uiIndex < tSwitch.count[uiBank]; ++uiIndex) {
    const CustomMidiCommand& tCommand = tSwitch.commands[uiBank][uiIndex];
    if (tCommand.type == CustomMidiCommandType::ControlChange)
      Axe.sendControlChange(tCommand.number, tCommand.value, tCommand.channel);
  }
  if (tSwitch.mode == CustomMidiMode::Alternate) {
    tSwitch.nextBank = 1 - uiBank;
    displayDirty = true;
  }
}

//=================================================================================================
// Function     : setCustomMidiMode
// Purpose      : Select one bank or alternating banks for a footswitch.
// Return Value : void
//=================================================================================================
void setCustomMidiMode(uint8_t uiSlot, CustomMidiMode eMode) {
  if (uiSlot >= NUM_SWITCHES || eMode > CustomMidiMode::Alternate) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[uiSlot];
  if (tSwitch.mode == eMode) return;
  tSwitch.mode = eMode;
  tSwitch.nextBank = 0;
  markCustomMidiChanged("Custom-MIDI-Modus zum Testen aktiv");
}

//=================================================================================================
// Function     : setCustomMidiCommand
// Purpose      : Save the single CC command assigned to a bank.
// Return Value : void
//=================================================================================================
void setCustomMidiCommand(uint8_t uiSlot, uint8_t uiBank, uint8_t uiIndex,
                          uint8_t uiChannel, uint8_t uiNumber, uint8_t uiValue) {
  if (uiSlot >= NUM_SWITCHES || uiBank >= CUSTOM_MIDI_BANKS || uiChannel < 1 || uiChannel > 16 ||
      uiNumber > 127 || uiValue > 127) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[uiSlot];
  if (uiIndex != 0) return;
  const CustomMidiCommand& tExisting = tSwitch.commands[uiBank][0];
  if (tSwitch.count[uiBank] && tExisting.type == CustomMidiCommandType::ControlChange &&
      tExisting.channel == uiChannel && tExisting.number == uiNumber && tExisting.value == uiValue) return;
  tSwitch.commands[uiBank][0] = {CustomMidiCommandType::ControlChange, uiChannel, uiNumber, uiValue};
  tSwitch.count[uiBank] = 1;
  tSwitch.nextBank = 0;
  markCustomMidiChanged("CC-Befehl zum Testen aktiv");
}

//=================================================================================================
// Function     : removeCustomMidiCommand
// Purpose      : Remove the CC command from a bank.
// Return Value : void
//=================================================================================================
void removeCustomMidiCommand(uint8_t uiSlot, uint8_t uiBank, uint8_t uiIndex) {
  if (uiSlot >= NUM_SWITCHES || uiBank >= CUSTOM_MIDI_BANKS) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[uiSlot];
  if (uiIndex != 0 || !tSwitch.count[uiBank]) return;
  tSwitch.count[uiBank] = 0;
  tSwitch.nextBank = 0;
  markCustomMidiChanged("CC-Befehl zum Testen entfernt");
}

//=================================================================================================
// Function     : saveCustomMidiConfiguration
// Purpose      : Persist all tested footswitch settings with one NVS write.
// Return Value : void
//=================================================================================================
void saveCustomMidiConfiguration() {
  if (!customMidiDirty) return;
  if (!saveCustomMidiSettings()) {
    lastOperation = "Custom MIDI konnte nicht gespeichert werden";
    return;
  }
  customMidiDirty = false;
  lastOperation = "Alle Custom-MIDI-Befehle gespeichert";
}
