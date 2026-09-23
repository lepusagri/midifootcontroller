#include "interface/AxeSystem.h"

#define SER_AVLB _serial->available()
#define SER_READ _serial->read()
#define SER_SEND _serial->write

void AxeSystem::begin(HardwareSerial &serial, byte midiChannel) {
  if (!_midiReady) {
    if (_startupDelay > 0) {
      delay(_startupDelay);
    }
    setMidiChannel(midiChannel);
    _serial = &serial;
    _serial->begin(MIDI_BAUD);
    _midiReady = true;
  }
}

void AxeSystem::readMidi() {
  // Bounded, non-blocking parser: incomplete messages resume on the next update.
  for (unsigned budget = 0; budget < 256 && SER_AVLB; ++budget) {
    byte data = SER_READ;
    if (data >= 0xF8) continue; // Realtime bytes may occur inside any message.
    if (data == 0xF0) {
      _readingSysex = true;
      _sysexCount = 0;
      _midiStatus = _midiDataCount = 0;
    }
    if (_readingSysex) {
      if (_sysexCount >= MAX_SYSEX) {
        _readingSysex = false;
        _sysexCount = 0;
        continue;
      }
      _sysexBuffer[_sysexCount++] = data;
      if (data == SYSEX_END) {
        _readingSysex = false;
        if (validateSysEx(_sysexBuffer, _sysexCount)) onSystemExclusive(_sysexBuffer, _sysexCount);
        _sysexCount = 0;
      } else if (data != 0xF0 && (data & 0x80)) {
        _readingSysex = false;
        _sysexCount = 0;
      }
      continue;
    }
    if (data & 0x80) {
      _midiStatus = data < 0xF0 ? data : 0;
      _midiDataCount = 0;
      continue;
    }
    if (!_midiStatus) continue;
    _midiData[_midiDataCount++] = data;
    byte kind = _midiStatus & 0xF0;
    byte needed = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
    if (_midiDataCount < needed) continue;
    _midiDataCount = 0;
    if (!filterMidiChannel(_midiStatus)) continue;
    if (kind == ControlChange && _midiData[0] == BANK_CHANGE_CC) _bank = _midiData[1];
    else if (kind == ProgramChange) onPresetChange(_bank * 128 + _midiData[0]);
  }
}

bool AxeSystem::filterMidiChannel(byte data) {
  return _midiChannel == MIDI_CHANNEL_OMNI || _midiChannel == ((data & 0x0F) + 1);
}

byte AxeSystem::applyMidiChannel(byte midiByte, byte channel) {
  if (channel == MIDI_CHANNEL_OMNI) {
    return midiByte;
  } else {
    return midiByte | ((channel - 1) & 0x0F);
  }
}

void AxeSystem::sendPresetChange(const PresetNumber number) {
  sendControlChange(BANK_CHANGE_CC, number / 128, _midiChannel);
  sendProgramChange(number % 128, _midiChannel);
  // Always confirm outgoing changes, including reloading the active preset when
  // the device does not echo a PC for an unchanged number.
  onPresetChange(number, false);
}

//One-based channel
void AxeSystem::sendControlChange(byte controller, byte value, byte channel) {
  SER_SEND(applyMidiChannel(ControlChange, channel));
  SER_SEND(controller);
  SER_SEND(value);
}

//One-based channel
void AxeSystem::sendProgramChange(byte value, byte channel) {
  SER_SEND(applyMidiChannel(ProgramChange, channel));
  SER_SEND(value);
}

void AxeSystem::sendSysEx(const byte *sysex, const byte length) {
  for (byte i = 0; i < length; i++) {
    SER_SEND(sysex[i]);
  }
#ifdef AXE_DEBUG_SYSEX
  debugSysex(sysex, length, "-> AxeSystem::sendSysEx():         ");
#endif
}

void AxeSystem::sendCommand(const byte command) {
  sendCommand(command, nullptr, 0);
}

void AxeSystem::sendCommand(const byte command, const byte *data, const byte paramCount) {

  byte length = 0;
  if (paramCount > MAX_SYSEX - 8) return;
  byte sysex[MAX_SYSEX];

  //header
  sysex[length++] = SystemExclusive;
  sysex[length++] = SYSEX_MANUFACTURER_BYTE1;
  sysex[length++] = SYSEX_MANUFACTURER_BYTE2;
  sysex[length++] = SYSEX_MANUFACTURER_BYTE3;
  sysex[length++] = _sysexFractalVersion;
  sysex[length++] = command;

  //optional data
  for (byte i = 0; i < paramCount; i++) {
    sysex[length++] = data[i];
  }

  //footer
  byte checksum = calculateChecksum(sysex, length);
  sysex[length++] = checksum;
  sysex[length++] = SYSEX_END;

  //punch it!
  sendSysEx(sysex, length);
}

byte AxeSystem::calculateChecksum(const byte *sysex, const byte length) {
  byte sum = sysex[0];
  for (int i = 1; i < length; i++) {
    sum ^= sysex[i];
  }
  return sum & 0x7F;
}

bool AxeSystem::validateSysEx(const byte *sysex, const byte length) {

  if (length < 8) return false;
  bool sysexOk =
                 sysex[0] == 0xF0 &&
                 sysex[1] == SYSEX_MANUFACTURER_BYTE1 &&
                 sysex[2] == SYSEX_MANUFACTURER_BYTE2 &&
                 sysex[3] == SYSEX_MANUFACTURER_BYTE3 &&
                 sysex[length - 1] == 0xF7;

  if (sysex[5] != SYSEX_TAP_TEMPO_PULSE && sysex[5] != SYSEX_TUNER) {
    sysexOk = sysexOk && sysex[length - 2] == calculateChecksum(sysex, length - 2);
  }

  return sysexOk;
}
