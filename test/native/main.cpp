#include <assert.h>
#include <string>
#include "ControllerLogic.h"
#include "AxeFxControl.h"

unsigned long testMillis = 0;
static HardwareSerial midi;
static AxeSystem axe(AxeSystem::FRACTAL_PRODUCT_FM3);
static std::string receivedName;
static int receivedPreset = -1, changes = 0, changingPreset = -1;
static void named(PresetNumber number, const char* name, byte) { receivedPreset = number; receivedName = name; }
static void changed(AxePreset) { ++changes; }
static void changing(PresetNumber number) { changingPreset = number; }
static void receive(byte command, const std::vector<byte>& payload) {
  std::vector<byte> packet = {0xf0, 0, 1, 0x74, 0x11, command};
  packet.insert(packet.end(), payload.begin(), payload.end());
  byte sum = 0;
  for (auto value : packet) sum ^= value;
  packet.push_back(sum & 0x7f);
  packet.push_back(0xf7);
  midi.input.insert(midi.input.end(), packet.begin(), packet.end());
  axe.update();
}
static void presetName(int number, const std::string& name) {
  std::vector<byte> data = {byte(number % 128), byte(number / 128)};
  data.insert(data.end(), name.begin(), name.end());
  data.push_back(0);
  receive(0x0d, data);
}
static void sceneName(int number, const std::string& name) {
  std::vector<byte> data = {byte(number - 1)};
  data.insert(data.end(), name.begin(), name.end());
  data.push_back(0);
  receive(0x0e, data);
}
int main() {
  const auto paddedScene = makeSceneLabel("Clean                           ", 1);
  assert(std::string(paddedScene.lines[0]) == "Clean" && paddedScene.textSize == 2);
  for (const char* empty : {"", "                                ", " ---  "}) {
    assert(std::string(makeSceneLabel(empty, 6).lines[0]) == "SCN 6");
  }
  assert(std::string(makeSceneLabel(nullptr, 2).lines[0]) == "SCN 2");
  for (size_t length = 1; length <= 32; ++length) {
    const std::string name(length, 'X');
    const auto label = makeSceneLabel(name.c_str(), 1);
    assert(std::string(label.lines[0]) + label.lines[1] == name);
    for (unsigned line = 0; line < label.lineCount; ++line)
      assert(strlen(label.lines[line]) * 6 * label.textSize <= 128);
  }
  const auto words = makeSceneLabel("Long scene name with two lines", 3);
  assert(std::string(words.lines[0]) == "Long scene name with");
  assert(std::string(words.lines[1]) == "two lines");
  int number = -1;
  assert(parseUnsigned("500", 500, number) && number == 500);
  assert(parseUnsigned("000", 500, number) && number == 0);
  for (auto invalid : {"", "-1", "501", "12abc", " 1", "9999999999999999999"}) assert(!parseUnsigned(invalid, 500, number));
  assert(!parseUnsigned(nullptr, 500, number));
  assert(scanDecision(false, 1499, 0) == ScanDecision::Wait);
  assert(scanDecision(false, 1500, 0) == ScanDecision::Timeout);
  assert(scanDecision(true, 500, 0) == ScanDecision::Received); // Empty names also count as a response.
  assert(scanDecision(true, 499, 0) == ScanDecision::Wait);
  assert(scanDecision(false, 1490, UINT32_MAX - 9) == ScanDecision::Timeout);
  const int favorites[] = {12, -1, 88, 3, -1, 500};
  assert(favoritePresetForSwitch(100, 0, 6, true, favorites, 501) == 12);
  assert(favoritePresetForSwitch(100, 1, 6, true, favorites, 501) == -1);
  assert(favoritePresetForSwitch(100, 5, 6, true, favorites, 501) == 500);
  assert(favoritePresetForSwitch(100, 0, 6, false, favorites, 501) == 98);
  const int emptyFavorites[] = {-1, -1, -1, -1, -1, -1};
  assert(favoritePresetForSwitch(100, 5, 6, true, emptyFavorites, 501) == 103);
  assert(favoritePresetForSwitch(0, 0, 6, false, favorites, 501) == -1);
  std::string json;
  appendJsonString(json, "Clean \"Wide\"\\Path\n\t");
  assert(json == "\"Clean \\\"Wide\\\"\\\\Path\\u000a\\u0009\"");

  axe.setStartupDelay(0);
  axe.begin(midi);
  axe.registerPresetNameCallback(named);
  axe.registerPresetChangeCallback(changed);
  axe.registerPresetChangingCallback(changing);
  axe.requestPresetDetails();
  presetName(7, "100% clean %s %n");
  assert(receivedPreset == 7 && receivedName == "100% clean %s %n");
  sceneName(3, "Scene 100%");
  assert(axe.getCurrentPreset().getSceneNumber() == 3);
  char copied[33];
  axe.getCurrentPreset().copyPresetName(copied, sizeof(copied));
  assert(std::string(copied) == "100% clean %s %n");
  sceneName(1, "Other scene");
  assert(axe.getCurrentPreset().getSceneNumber() == 3); // Name enumeration cannot change the active scene.
  receive(0x0c, {1});
  sceneName(2, "New active");
  assert(axe.getCurrentPreset().getSceneNumber() == 2);
  assert(std::string(axe.getCurrentPreset().getSceneName()) == "New active");
  int previousChanges = changes;
  axe.requestPresetDetails();
  presetName(7, "100% clean %s %n");
  sceneName(2, "New active");
  assert(changes == previousChanges + 1); // Reloading the same preset still confirms completion.
  axe.requestPresetDetails();
  presetName(8, std::string(32, 'X'));
  assert(receivedPreset == 8 && receivedName.size() == 32);
  axe.requestPresetDetails();
  presetName(9, "");
  assert(receivedPreset == 9 && receivedName.empty());
  axe.fetchEffects(true);
  axe.requestPresetDetails();
  presetName(10, "Effects");
  sceneName(1, "Rhythm");
  receive(0x13, {byte(ID_COMP1 % 128), byte(ID_COMP1 / 128), 0});
  assert(axe.getCurrentPreset().hasEffect(ID_COMP1));
  axe.requestPresetDetails();
  presetName(11, "Empty grid");
  sceneName(1, "Clean");
  receive(0x13, {});
  assert(!axe.getCurrentPreset().hasEffect(ID_COMP1));
  std::vector<byte> fullDump;
  for (int i = 0; i < 50; ++i) {
    fullDump.push_back(byte(ID_COMP1 % 128));
    fullDump.push_back(byte(ID_COMP1 / 128));
    fullDump.push_back(0);
  }
  receive(0x13, fullDump);
  assert(axe.getCurrentPreset().getEffectCount() == 50);
  midi.input = {0xc0};
  axe.update(); // Must return immediately for an incomplete message.
  midi.input = {0xf8, 10};
  axe.update();
  assert(changingPreset == 10);
  midi.input = {11}; // Running status.
  axe.update();
  assert(changingPreset == 11);
  midi.input = {0xb0, 7}; // An unrelated CC must consume its value, too.
  axe.update();
  midi.input = {100, 0xb0, 0, 1, 0xc0, 3};
  axe.update();
  assert(changingPreset == 131);
  midi.input.push_back(0xf0);
  for (int i = 0; i < 220; ++i) midi.input.push_back(1);
  midi.input.push_back(0xf7);
  midi.input.push_back(0xc0);
  midi.input.push_back(4);
  axe.update();
  assert(changingPreset == 132); // Recovers after an overlong SysEx.
  receive(0x0d, {}); // Truncated payload is ignored.
  axe.sendPresetChange(15); // Still requests details after receiving MIDI PC echoes.
  assert(changingPreset == 15);
  presetName(15, "Reload");
  sceneName(1, "Scene");
  receive(0x13, {});
  assert(axe.getCurrentPreset().getPresetNumber() == 15);
  puts("PASS: parameters, JSON, scanner deadlines, names, scenes and streaming MIDI");
}
