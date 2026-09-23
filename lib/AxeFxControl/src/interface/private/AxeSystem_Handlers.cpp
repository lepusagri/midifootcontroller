#include "interface/AxeSystem.h"

void AxeSystem::onPresetChange(const PresetNumber number, bool receivedFromAxe) {

  // If we're in AUTO mode and we get a PC message from the Axe, then we
  // know it's telling about preset changes, so OK to switch to PASSIVE.
  if (receivedFromAxe && _updateMode == UpdateMode::AUTO) {
    _updateMode = UpdateMode::PASSIVE_WAIT_FOR_AXE;
  }

  _incomingPreset.reset();
  _presetDetailsReported = false;
  _incomingPreset.setPresetNumber(number);
  requestPresetName(number);
  callPresetChangingCallback(number);
}

void AxeSystem::onSystemExclusive(const byte *sysex, const byte length) {

  if (callSysexPluginCallback(sysex, length)) {
    return;
  }

  if (length < 8) return;
  byte command = sysex[5];
  if ((command == SYSEX_REQUEST_PRESET_INFO && length < 10) ||
      (command == SYSEX_REQUEST_SCENE_INFO && length < 9) ||
      (command == SYSEX_REQUEST_SCENE_NUMBER && length < 9) ||
      (command == SYSEX_REQUEST_FIRMWARE && length < 12) ||
      (command == SYSEX_REQUEST_TEMPO && length < 10) ||
      (command == SYSEX_TUNER && length < 11) ||
      (command == SYSEX_REQUEST_LOOPER_STATUS && length < 9)) return;
  const size_t bufferSize = 40;
  char buffer[bufferSize];

  checkTimers();
  _lastSysexResponse = millis();

#ifdef AXE_DEBUG_SYSEX
  debugSysex(sysex, length, "<- AxeSystem::onSystemExclusive(): ");
#endif

  switch (command) {

  case SYSEX_TAP_TEMPO_PULSE: {
    if (!_tunerEngaged) {
      callTapTempoCallback();
    }
    break;
  }

  case SYSEX_REQUEST_PRESET_INFO: {

    _lastRefresh = millis();
    const byte max = AxePreset::MAX_PRESET_NAME + 1;
    const PresetNumber number = midiBytesToInt(sysex[6], sysex[7]);

    if (isRequestedPreset(number)) { // drop older responses
      parseName(sysex, length, 8, buffer, max);
      _incomingPreset.reset(); // in case stale packets arrived from prev request
      _presetDetailsReported = false;
      _incomingPreset.setPresetNumber(number);
      _incomingPreset.setPresetName(buffer);
      _incomingPreset.copyPresetName(buffer, max); // copy back out in case preset changed it
      callPresetNameCallback(number, (const char *)buffer, max);
      requestSceneName(); // next item in chain
      checkIncomingPreset();
    } else {

      if (shouldParseStalePresets()) {
        
        //only parse if a callback is registered
        parseName(sysex, length, 8, buffer, max);
        callStalePresetNameCallback(number, (const char *)buffer, max);

      } else {
#ifdef AXE_DEBUG
      DEBUGGER.print("Dropping stale preset name packet ");
      DEBUGGER.print(number);
      DEBUGGER.print(", waiting for ");
      DEBUGGER.println(_incomingPreset.getPresetNumber());
#endif
      }
    }

    break;
  }

  case SYSEX_REQUEST_SCENE_INFO: {
    const SceneNumber number = sysex[6] + 1;
    const byte max = AxePreset::MAX_SCENE_NAME + 1;
    parseName(sysex, length, 7, buffer, max);

    if (_incomingPreset.getSceneNumber() == -1 || _incomingPreset.getSceneNumber() == number) {

      // only set these for first response, next responses will be from
      // requestAllSceneNames()
      _incomingPreset.setSceneNumber(number);
      _incomingPreset.setSceneName(buffer);
      _incomingPreset.copySceneName(buffer, max); // copy back out in case preset changed it

      if (_fetchEffects) {
        requestEffectDetails(); // ask here instead of in preset name to avoid filling rx buffer
      } else {
        _incomingPreset._effectCount = 0;
      }
    }

    callSceneNameCallback(number, (const char *)buffer, max);
    checkIncomingPreset();

    break;
  }

  case SYSEX_REQUEST_SCENE_NUMBER: {
    _incomingPreset.setSceneNumber(sysex[6] + 1);
    requestSceneName();
    checkIncomingPreset();
    break;
  }

  case SYSEX_EFFECT_DUMP: {
    processEffectDump(sysex, length);
    callEffectsReceivedCallback(&_incomingPreset);
    checkIncomingPreset();
    break;
  }

  case SYSEX_REQUEST_FIRMWARE: {
    _firmwareVersion.major = sysex[6];
    _firmwareVersion.minor = sysex[7];
    _usbVersion.major = sysex[9];
    _usbVersion.minor = sysex[10];
    callSystemChangeCallback();
    break;
  }

  case SYSEX_REQUEST_TEMPO: {
    byte newTempo = (byte)midiBytesToInt(sysex[6], sysex[7]);
    if (newTempo != _tempo) {
      _tempo = newTempo;
      callSystemChangeCallback();
    }
    break;
  }

  case SYSEX_TUNER: {
    _lastTunerResponse = millis();
    if (!_tunerEngaged && ++_tunerIncomingCount > _tunerTriggerThreshold) {
      _tunerEngaged = true;
      _tunerIncomingCount = 0;
      callTunerStatusCallback(_tunerEngaged);
    }

    if (_tunerEngaged) {
      byte note = sysex[6];
      byte string = sysex[7] + 1;
      byte fineTune = sysex[8];
      //Fix #17
      if (note <= MAX_TUNER_NOTE && string <= MAX_TUNER_STRING && fineTune <= MAX_TUNER_FINETUNE) {
        callTunerDataCallback(_notes[note], string, fineTune);
      } else {
#ifdef AXE_DEBUG
        DEBUGGER.print("******** INVALID TUNER DATA: ");
        DEBUGGER.print(" note=");
        DEBUGGER.print(note);
        DEBUGGER.print(" string=");
        DEBUGGER.print(string);
        DEBUGGER.print(" fineTune=");
        DEBUGGER.println(fineTune);
#endif
      }
    }

    break;
  }

  case SYSEX_REQUEST_LOOPER_STATUS: {
    LooperStatus newStatus = sysex[6];
    if (_looper.getStatus() != newStatus) {
      _looper.setStatus(newStatus);
      callLooperStatusCallback(&_looper);
    }
    break;
  }

  case SYSEX_REQUEST_EFFECT_BYPASS: {
    // adding so it's not unhandled during debug...
    // not much to do because Axe doesn't send status updates for this
    break;
  }

  default: {
#ifdef AXE_DEBUG_SYSEX
    DEBUGGER.println(F("******** AxeSystem::onSystemExclusive(): UNHANDLED SYSEX: "));
    char d[100];
    for (byte i = 1; i < length - 1; i++) {
      snprintf(d, 6, "0x%02X ", sysex[i]);
      DEBUGGER.print(d);
    }
    DEBUGGER.println();
    DEBUGGER.println(F(" ********"));
#endif
  }

  }; // end case
}

bool AxeSystem::isRequestedPreset(const PresetNumber number) {
  return _incomingPreset.getPresetNumber() == -1 ||
         _incomingPreset.getPresetNumber() == number;
}

void AxeSystem::checkIncomingPreset() {
  if (_incomingPreset.isComplete() && (!_presetDetailsReported || !_preset.equals(_incomingPreset))) {
    _preset = _incomingPreset;
    _presetDetailsReported = true;
    callPresetChangeCallback(&_preset);
  }
}

void AxeSystem::processEffectDump(const byte *sysex, const byte length) {

  unsigned count = 0;
  if (length < 8 || (length - 8) % 3 != 0) return;
  AxeEffect effects[AxePreset::MAX_EFFECTS];

  for (byte i = 6; i < length - 3 && count < AxePreset::MAX_EFFECTS; i += 3) {

    EffectId effectId = sysex[i];
    byte msb = sysex[i + 1], status = sysex[i + 2];
    bool bypassed = !!(status & 1);
    Channel channel = (status >> 1) & 0x03;
    byte numChannels = (status >> 4) & 0x07;
    if (msb)
      effectId |= 128;

    AxeEffect effect;
    effect.setAxeSystem(this);
    effect.setEffectId(effectId);
    effect.setBypassed(bypassed);
    effect.setChannel(channel);
    effect.setChannelCount(numChannels);

    // assumes preset number has already been received
    if (callEffectFilterCallback(_incomingPreset.getPresetNumber(), effect)) {
      effects[count++] = effect;
    }
  }

  _incomingPreset.setEffects(effects, count);
}

void AxeSystem::parseName(const byte *sysex, const byte length, const byte offset, char *buffer, const byte size) {
  if (!size) return;
  memset(buffer, 0, size);
  byte count = 0;
  for (byte i = offset; i < length - 2 && count < size - 1; i++) {
    if (sysex[i] == 0)
      break;
    buffer[count++] = sysex[i];
  }
  buffer[count] = '\0';
}

bool AxeSystem::isValidPresetNumber(const PresetNumber preset) {
  return preset >= 0 && preset <= maxPresets();
}

bool AxeSystem::isValidSceneNumber(const SceneNumber scene) {
  return scene > 0 && scene <= MAX_SCENES;
}
