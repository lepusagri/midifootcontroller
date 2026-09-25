#include "Controller.h"
#include <Preferences.h>
#include <string.h>

#define CUSTOM_MIDI_MAGIC 0x4C434D49u
#define CUSTOM_MIDI_VERSION 3u
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
} T_STORED_MIDI_SETTINGS_V1;

typedef struct {
  uint32_t uiMagic;
  uint8_t uiVersion;
  T_STORED_MIDI_SWITCH tSwitches[NUM_SWITCHES];
  char sNames[NUM_SWITCHES][CUSTOM_MIDI_NAME_LENGTH + 1];
} T_STORED_MIDI_SETTINGS_V2;

typedef struct {
  uint32_t uiMagic;
  uint8_t uiVersion;
  T_STORED_MIDI_SWITCH tSwitches[CUSTOM_MIDI_SETS][NUM_SWITCHES];
  char sNames[CUSTOM_MIDI_SETS][NUM_SWITCHES][CUSTOM_MIDI_NAME_LENGTH + 1];
} T_STORED_MIDI_SETTINGS;

CustomMidiSwitch customMidiSwitches[CUSTOM_MIDI_SETS][NUM_SWITCHES];
uint8_t activeCustomMidiSet = 0;
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
  for (uint8_t uiSet = 0; uiSet < CUSTOM_MIDI_SETS; ++uiSet) {
    for (uint8_t uiSlot = 0; uiSlot < NUM_SWITCHES; ++uiSlot) {
      const CustomMidiSwitch& tSource = customMidiSwitches[uiSet][uiSlot];
      T_STORED_MIDI_SWITCH& tTarget = tStored.tSwitches[uiSet][uiSlot];
      tTarget.uiMode = static_cast<uint8_t>(tSource.mode);
      memcpy(tStored.sNames[uiSet][uiSlot], tSource.name, sizeof(tSource.name));
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
  T_STORED_MIDI_SETTINGS_V2 tPrevious = {};
  T_STORED_MIDI_SETTINGS_V1 tLegacy = {};
  Preferences tPreferences;
  bool bLoaded = false;
  size_t iLength = 0;
  // +++++++++++++++++++++++++++++++++
  if (!tPreferences.begin("lepus-custom", true)) return;
  iLength = tPreferences.getBytesLength("settings");
  if (iLength == sizeof(tStored)) {
    bLoaded = tPreferences.getBytes("settings", &tStored, sizeof(tStored)) == sizeof(tStored);
  } else if (iLength == sizeof(tPrevious)) {
    bLoaded = tPreferences.getBytes("settings", &tPrevious, sizeof(tPrevious)) == sizeof(tPrevious);
    if (bLoaded) {
      tStored.uiMagic = tPrevious.uiMagic;
      tStored.uiVersion = tPrevious.uiVersion;
      memcpy(tStored.tSwitches[0], tPrevious.tSwitches, sizeof(tPrevious.tSwitches));
      memcpy(tStored.sNames[0], tPrevious.sNames, sizeof(tPrevious.sNames));
    }
  } else if (iLength == sizeof(tLegacy)) {
    bLoaded = tPreferences.getBytes("settings", &tLegacy, sizeof(tLegacy)) == sizeof(tLegacy);
    if (bLoaded) {
      tStored.uiMagic = tLegacy.uiMagic;
      tStored.uiVersion = tLegacy.uiVersion;
      memcpy(tStored.tSwitches[0], tLegacy.tSwitches, sizeof(tLegacy.tSwitches));
    }
  }
  tPreferences.end();
  if (!bLoaded || tStored.uiMagic != CUSTOM_MIDI_MAGIC ||
      (iLength == sizeof(tLegacy) && tStored.uiVersion != 1) ||
      (iLength == sizeof(tPrevious) && tStored.uiVersion != 2) ||
      (iLength == sizeof(tStored) && tStored.uiVersion != CUSTOM_MIDI_VERSION)) return;
  for (uint8_t uiSet = 0; uiSet < CUSTOM_MIDI_SETS; ++uiSet) {
    for (uint8_t uiSlot = 0; uiSlot < NUM_SWITCHES; ++uiSlot) {
      const T_STORED_MIDI_SWITCH& tSwitch = tStored.tSwitches[uiSet][uiSlot];
      if (tSwitch.uiMode > static_cast<uint8_t>(CustomMidiMode::Alternate)) return;
      if (iLength != sizeof(tLegacy)) {
        bool bTerminated = false;
        for (uint8_t uiIndex = 0; uiIndex <= CUSTOM_MIDI_NAME_LENGTH; ++uiIndex) {
          const char cChar = tStored.sNames[uiSet][uiSlot][uiIndex];
          if (!cChar) { bTerminated = true; break; }
          if (cChar < ' ' || cChar > '~') return;
        }
        if (!bTerminated) return;
      }
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
  }
  for (uint8_t uiSet = 0; uiSet < CUSTOM_MIDI_SETS; ++uiSet) {
    for (uint8_t uiSlot = 0; uiSlot < NUM_SWITCHES; ++uiSlot) {
      CustomMidiSwitch& tTarget = customMidiSwitches[uiSet][uiSlot];
      const T_STORED_MIDI_SWITCH& tSource = tStored.tSwitches[uiSet][uiSlot];
      memcpy(tTarget.name, tStored.sNames[uiSet][uiSlot], sizeof(tTarget.name));
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
}

//=================================================================================================
// Function     : triggerCustomMidi
// Purpose      : Send the selected bank once on a short footswitch release.
// Return Value : void
//=================================================================================================
void triggerCustomMidi(uint8_t uiSlot) {
  if (uiSlot >= NUM_SWITCHES) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[activeCustomMidiSet][uiSlot];
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
void setCustomMidiMode(uint8_t uiSet, uint8_t uiSlot, CustomMidiMode eMode) {
  if (uiSet >= CUSTOM_MIDI_SETS || uiSlot >= NUM_SWITCHES || eMode > CustomMidiMode::Alternate) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[uiSet][uiSlot];
  if (tSwitch.mode == eMode) return;
  tSwitch.mode = eMode;
  tSwitch.nextBank = 0;
  markCustomMidiChanged("Custom-MIDI-Modus zum Testen aktiv");
}

//=================================================================================================
// Function     : setCustomMidiName
// Purpose      : Activate a footswitch label in RAM for OLED testing.
// Return Value : void
//=================================================================================================
void setCustomMidiName(uint8_t uiSet, uint8_t uiSlot, const char* psName) {
  if (uiSet >= CUSTOM_MIDI_SETS || uiSlot >= NUM_SWITCHES || !psName) return;
  size_t iLength = 0;
  while (psName[iLength] && iLength <= CUSTOM_MIDI_NAME_LENGTH) {
    if (psName[iLength] < ' ' || psName[iLength] > '~') return;
    ++iLength;
  }
  if (iLength > CUSTOM_MIDI_NAME_LENGTH) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[uiSet][uiSlot];
  if (strcmp(tSwitch.name, psName) == 0) return;
  memcpy(tSwitch.name, psName, iLength + 1);
  markCustomMidiChanged("Custom-MIDI-Name zum Testen aktiv");
}

//=================================================================================================
// Function     : setCustomMidiCommand
// Purpose      : Save the single CC command assigned to a bank.
// Return Value : void
//=================================================================================================
void setCustomMidiCommand(uint8_t uiSet, uint8_t uiSlot, uint8_t uiBank, uint8_t uiIndex,
                          uint8_t uiChannel, uint8_t uiNumber, uint8_t uiValue) {
  if (uiSet >= CUSTOM_MIDI_SETS || uiSlot >= NUM_SWITCHES || uiBank >= CUSTOM_MIDI_BANKS || uiChannel < 1 || uiChannel > 16 ||
      uiNumber > 127 || uiValue > 127) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[uiSet][uiSlot];
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
void removeCustomMidiCommand(uint8_t uiSet, uint8_t uiSlot, uint8_t uiBank, uint8_t uiIndex) {
  if (uiSet >= CUSTOM_MIDI_SETS || uiSlot >= NUM_SWITCHES || uiBank >= CUSTOM_MIDI_BANKS) return;
  CustomMidiSwitch& tSwitch = customMidiSwitches[uiSet][uiSlot];
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
  lastOperation = "Alle Custom-MIDI-Einstellungen gespeichert";
}
