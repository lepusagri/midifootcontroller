let busy = false;
let polling = false;
let targetPresetValue = 0;
let userIsEditing = false;
let editTimeout;
let cacheRevision = -1;
let presetCount = 501;
let lastState;
let uiError = '';

const el = id => document.getElementById(id);
async function api(path, method = 'GET') {
  const response = await fetch(path, {method, cache: 'no-store'});
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}
function updatePickerUI() {
  el('d-hund').textContent = Math.floor(targetPresetValue / 100);
  el('d-zehn').textContent = Math.floor(targetPresetValue % 100 / 10);
  el('d-ein').textContent = targetPresetValue % 10;
  el('go-btn').textContent = userIsEditing ? `Preset ${targetPresetValue} laden` : 'Preset laden';
}
function changeDigit(weight, direction) {
  const next = targetPresetValue + weight * direction;
  if (next < 0 || next >= presetCount || busy || lastState?.scanning) return;
  targetPresetValue = next;
  userIsEditing = true;
  clearTimeout(editTimeout);
  editTimeout = setTimeout(() => { userIsEditing = false; loadStatus(); }, 10000);
  updatePickerUI();
  el('preset-list-select').value = String(next);
}
function setControls() {
  const scanning = !!lastState?.scanning;
  for (const button of document.querySelectorAll('.navgrid button, .btn-step, #go-btn')) {
    button.disabled = busy || scanning;
  }
  el('preset-list-select').disabled = busy || scanning;
  el('scan-smart-btn').disabled = el('scan-deep-btn').disabled = busy;
  el('scan-stop-btn').disabled = busy;
  el('save-flash-btn').disabled = busy || !lastState?.storageReady || !lastState?.unsaved;
  for (const button of document.querySelectorAll('#scenegrid button, #grid button')) {
    button.disabled = busy || scanning || !lastState?.ready || button.dataset.available === 'false';
  }
}
async function sendAction(path) {
  if (busy) return;
  busy = true;
  uiError = '';
  setControls();
  try {
    await api(path, 'POST');
    el('status').textContent = 'Befehl angenommen …';
  } catch (error) {
    uiError = error.message;
    el('status').textContent = uiError;
  } finally {
    busy = false;
    setControls();
  }
}
async function executePresetChange(number) {
  if (!Number.isInteger(number) || number < 0 || number >= presetCount) return;
  await sendAction(`/api/preset?number=${number}`);
}
async function selectBoxChanged(value) {
  targetPresetValue = Number(value);
  userIsEditing = false;
  clearTimeout(editTimeout);
  updatePickerUI();
  await executePresetChange(targetPresetValue);
}
async function submitPreset() {
  userIsEditing = false;
  clearTimeout(editTimeout);
  await executePresetChange(targetPresetValue);
}
async function refreshCache(revision) {
  if (cacheRevision === revision) return;
  const data = await api('/api/presets');
  const select = el('preset-list-select');
  const scroll = select.scrollTop;
  const selected = select.value;
  select.replaceChildren();
  data.presets.forEach((preset, index) => {
    const option = document.createElement('option');
    option.value = String(index);
    const name = preset.state === 0 ? '[unbekannt]' : preset.state === 2 ? '[keine Antwort]' : preset.name.trim() || '[leer]';
    option.textContent = `${String(index).padStart(3, '0')} : ${name}`;
    select.appendChild(option);
  });
  select.value = selected;
  select.scrollTop = scroll;
  cacheRevision = data.revision;
}
function updateButtons(container, items, scene) {
  // Keep DOM nodes stable while polling; do not remove a button under the pointer.
  const parent = el(container);
  items.forEach((item, index) => {
    let button = parent.children[index];
    if (!button) {
      button = document.createElement('button');
      button.type = 'button';
      parent.appendChild(button);
    }
    button.textContent = scene ? `${item.number} · ${item.name || 'Szene ' + item.number}` : item.label;
    button.dataset.available = String(scene || item.available);
    button.className = !scene && !item.available ? 'unavailable' : item.active ? 'on' : 'off';
    button.setAttribute('aria-pressed', String(item.active));
    button.onclick = () => scene ? setSc(item.number) : tog(item.index);
  });
}
async function loadStatus() {
  if (polling || busy) return;
  polling = true;
  try {
    const state = await api('/api/status');
    lastState = state;
    presetCount = state.presetCount;
    el('preset').textContent = state.presetNumber >= 0 ? `${state.presetNumber} · ${state.presetName || '[leer]'}` : 'Warte auf MIDI …';
    el('scene').textContent = state.sceneNumber >= 1 ? `${state.sceneNumber} · ${state.sceneName || '[leer]'}` : '—';
    await refreshCache(state.cacheRevision);
    if (!userIsEditing && !state.scanning && state.presetNumber >= 0) {
      targetPresetValue = state.presetNumber;
      updatePickerUI();
      el('preset-list-select').value = String(targetPresetValue);
    }
    updateButtons('scenegrid', state.scenes, true);
    updateButtons('grid', state.slots, false);
    el('save-flash-btn').textContent = !state.storageReady ? 'Preset-Speicher nicht verfügbar' : state.unsaved ? 'Geänderte Namen speichern' : 'Alle Namen gespeichert';
    el('scan-smart-btn').style.display = el('scan-deep-btn').style.display = state.scanning ? 'none' : 'block';
    el('scan-stop-btn').style.display = state.scanning ? 'block' : 'none';
    el('scan-stop-btn').textContent = `Scan stoppen (${state.scanProgress} / ${state.presetCount})`;
    el('status').textContent = uiError || state.message || (state.ready ? 'Verbunden' : 'Warte auf MIDI …');
    setControls();
  } catch (error) {
    el('status').textContent = `Verbindung unterbrochen: ${error.message}`;
  } finally { polling = false; }
}
async function tog(index) { await sendAction(`/api/effect?slot=${index}`); }
async function setSc(number) { await sendAction(`/api/scene?number=${number}`); }
async function sendCmd(command) { await sendAction(`/api/command?cmd=${encodeURIComponent(command)}`); }
async function saveToFlash() { await sendAction('/api/save'); }
async function startScan(type) {
  const message = type === 'deep'
    ? 'Alle Presets neu einlesen? Das Gerät wechselt dabei die Presets. Dauer etwa 4–13 Minuten. Anschließend wird das vorherige Preset wieder geladen.'
    : 'Unbekannte Presets einlesen? Währenddessen ist die Bedienung gesperrt. Anschließend wird das vorherige Preset wieder geladen.';
  if (confirm(message)) await sendAction(`/api/scan/start?mode=${type}`);
}
async function stopScan() { await sendAction('/api/scan/stop'); }
loadStatus();
setInterval(loadStatus, 500);
