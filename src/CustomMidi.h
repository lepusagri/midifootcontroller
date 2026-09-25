#pragma once
#include <stdint.h>

constexpr uint8_t CUSTOM_MIDI_BANKS = 2;
constexpr uint8_t CUSTOM_MIDI_MAX_COMMANDS = 1;
constexpr uint8_t CUSTOM_MIDI_NAME_LENGTH = 20;
constexpr uint8_t CUSTOM_MIDI_SETS = 2;

enum class CustomMidiMode : uint8_t { Single = 0, Alternate = 1 };
enum class CustomMidiCommandType : uint8_t { ControlChange = 0 };

struct CustomMidiCommand {
  CustomMidiCommandType type = CustomMidiCommandType::ControlChange;
  uint8_t channel = 1;
  uint8_t number = 0;
  uint8_t value = 0;
  CustomMidiCommand(CustomMidiCommandType t = CustomMidiCommandType::ControlChange,
                    uint8_t ch = 1, uint8_t no = 0, uint8_t val = 0)
      : type(t), channel(ch), number(no), value(val) {}
};

struct CustomMidiSwitch {
  char name[CUSTOM_MIDI_NAME_LENGTH + 1] = {};
  CustomMidiMode mode = CustomMidiMode::Single;
  uint8_t count[CUSTOM_MIDI_BANKS] = {};
  CustomMidiCommand commands[CUSTOM_MIDI_BANKS][CUSTOM_MIDI_MAX_COMMANDS] = {};
  uint8_t nextBank = 0;
};

extern CustomMidiSwitch customMidiSwitches[CUSTOM_MIDI_SETS][6];
extern uint8_t activeCustomMidiSet;
extern uint32_t customMidiRevision;
extern bool customMidiDirty;

void loadCustomMidiSettings();
void triggerCustomMidi(uint8_t slot);
void setCustomMidiMode(uint8_t set, uint8_t slot, CustomMidiMode mode);
void setCustomMidiName(uint8_t set, uint8_t slot, const char* name);
void setCustomMidiCommand(uint8_t set, uint8_t slot, uint8_t bank, uint8_t index,
                          uint8_t channel, uint8_t number, uint8_t value);
void removeCustomMidiCommand(uint8_t set, uint8_t slot, uint8_t bank, uint8_t index);
void saveCustomMidiConfiguration();
