const el = id => document.getElementById(id);
const effectNames = {CMP:'Kompressor', DRV:'Drive', MOD:'Modulation', DLY:'Delay', REV:'Reverb', BST:'Boost'};
const viewNames = ['spielen', 'presets', 'custom-midi', 'verwaltung'];
let busy = false;
let polling = false;
let online = false;
let lastState;
let presets = [];
let cacheRevision = -1;
let presetCount = 501;
let activePreset = -1;
let renderedFavoritesRevision = -1;
let actionVersion = 0;
let inputEdited = false;
let uiError = '';
let errorSource = '';
let notice = '';
let noticeUntil = 0;
let currentView = 'spielen';
let networkLoaded = false;
let renderedCustomMidiRevision = -1;
let renderedCustomMidiSlot = -1;
const customMidiDrafts = new Map();
const customMidiNameDrafts = new Map();

function text(node, value) {
  const next = String(value);
  if (node.textContent !== next) node.textContent = next;
}
async function api(path, method = 'GET', body) {
  const abort = new AbortController();
  const timer = setTimeout(() => abort.abort(), 4500);
  try {
    const response = await fetch(path, {method, body, cache:'no-store', signal:abort.signal});
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
    return data;
  } catch (error) {
    if (error.name === 'AbortError') throw new Error('Der Controller antwortet nicht.');
    throw error;
  } finally { clearTimeout(timer); }
}

function setView() {
  const hash = location.hash.slice(1);
  currentView = viewNames.includes(hash) ? hash : 'spielen';
  for (const link of document.querySelectorAll('.view-nav a')) {
    if (link.dataset.view === currentView) link.setAttribute('aria-current', 'page');
    else link.removeAttribute('aria-current');
  }
  el('workspace').dataset.view = currentView;
  el('performance').hidden = currentView !== 'spielen';
  el('preset-browser').hidden = currentView === 'verwaltung' || currentView === 'custom-midi';
  el('management').hidden = currentView !== 'verwaltung';
  el('custom-midi-settings').hidden = currentView !== 'custom-midi';
  if (currentView === 'presets') centerActivePreset();
}

function commandBlocked() { return busy || !online || !lastState || lastState.scanning; }
function setControls() {
  const blocked = commandBlocked();
  for (const id of ['preset-down', 'preset-up']) el(id).disabled = blocked || lastState.presetNumber < 0;
  el('go-btn').disabled = blocked;
  const favoritesFull = Array.isArray(lastState?.favorites) && lastState.favorites.every(number => number >= 0);
  for (const button of document.querySelectorAll('.preset-select')) button.disabled = blocked;
  for (const button of document.querySelectorAll('.favorite-toggle')) {
    button.disabled = blocked || (favoritesFull && button.getAttribute('aria-pressed') !== 'true');
  }
  for (const button of document.querySelectorAll('.favorite-slot-actions button')) button.disabled = blocked || button.dataset.available === 'false';
  el('favorite-mode-btn').disabled = blocked;
  for (const button of document.querySelectorAll('#scenegrid button, #grid button')) {
    button.disabled = blocked || !lastState.ready || button.dataset.available === 'false';
  }
  el('scan-smart-btn').disabled = el('scan-deep-btn').disabled = blocked || !lastState?.ready;
  el('scan-stop-btn').disabled = busy || !online || !lastState?.scanning;
  el('save-flash-btn').disabled = busy || !online || !lastState?.storageReady || !lastState?.unsaved;
  el('network-save').disabled = busy || !online || !networkLoaded;
  el('custom-midi-mode').disabled = blocked;
  const nameDraft = customMidiNameDrafts.get(Number(el('custom-midi-slot').value));
  el('custom-midi-name-submit').disabled = blocked || !nameDraft || nameDraft.pending;
  el('custom-midi-name').disabled = !!nameDraft?.pending;
  el('custom-midi-save').disabled = blocked || !lastState?.customMidiDirty || customMidiDrafts.size > 0 || customMidiNameDrafts.size > 0;
  for (const button of document.querySelectorAll('#custom-midi-banks button')) {
    button.disabled = blocked || button.dataset.pending === 'true';
  }
}

function updateNetworkFields() {
  const home = document.querySelector('input[name="network-mode"]:checked').value === 'home';
  el('network-home').hidden = !home;
  el('network-static').hidden = !home || el('network-dhcp').checked;
}

async function loadNetworkSettings() {
  if (networkLoaded) return;
  try {
    const settings = await api('/api/network');
    document.querySelector(`input[name="network-mode"][value="${settings.mode}"]`).checked = true;
    el('network-ssid').value = settings.ssid;
    el('network-dhcp').checked = settings.dhcp;
    for (const key of ['ip', 'gateway', 'subnet', 'dns']) el(`network-${key}`).value = settings[key];
    text(el('network-state'), settings.activeMode === 'ap'
      ? `Access Point ${settings.apSsid} · ${settings.address}${settings.mode === 'home' ? ' (Heimnetz nicht erreichbar)' : ''}`
      : `Heimnetz ${settings.ssid} · ${settings.address}`);
    el('network-password').placeholder = settings.hasPassword ? 'Gespeichertes Passwort beibehalten' : 'Offenes Netz oder Passwort eingeben';
    networkLoaded = true;
    updateNetworkFields();
    setControls();
  } catch (error) {
    text(el('network-state'), `Netzwerkstatus nicht verfügbar: ${error.message}`);
  }
}

async function saveNetwork(event) {
  event.preventDefault();
  if (busy || !online || !networkLoaded) return;
  const mode = document.querySelector('input[name="network-mode"]:checked').value;
  const params = new URLSearchParams({
    mode, ssid:el('network-ssid').value.trim(), password:el('network-password').value,
    dhcp:el('network-dhcp').checked ? '1' : '0', ip:el('network-ip').value.trim(),
    gateway:el('network-gateway').value.trim(), subnet:el('network-subnet').value.trim(),
    dns:el('network-dns').value.trim()
  });
  el('network-error').hidden = true;
  busy = true;
  setControls();
  try {
    await api('/api/network', 'POST', params);
    text(el('network-state'), 'Gespeichert. Der Controller startet neu. Verbinde dein Handy danach mit dem gewählten Netzwerk.');
  } catch (error) {
    text(el('network-error'), error.message);
    el('network-error').hidden = false;
  } finally {
    busy = false;
    setControls();
  }
}

function renderStatus() {
  const scanning = !!lastState?.scanning;
  const connected = online && lastState?.ready;
  el('connection').dataset.state = !online ? 'offline' : connected || scanning ? 'online' : 'waiting';
  text(el('connection'), !online ? 'Verbindung unterbrochen' : scanning ? 'Scan läuft' : connected ? 'Verbunden' : 'Warte auf MIDI');
  el('statusbar').dataset.error = String(!!uiError);
  el('retry-btn').hidden = !uiError;
  const fallback = !online ? 'Der Controller ist nicht erreichbar.' : scanning
    ? `Scan läuft: ${lastState.scanProgress} / ${presetCount}. Die Spielsteuerung ist gesperrt.`
    : lastState?.message || (connected ? 'Bereit' : 'Warte auf eine vollständige MIDI-Antwort.');
  text(el('status'), uiError || (Date.now() < noticeUntil ? notice : fallback));
}

async function sendAction(path) {
  if (busy || !online) return false;
  busy = true;
  ++actionVersion;
  uiError = '';
  errorSource = '';
  notice = 'Befehl wird gesendet …';
  noticeUntil = Date.now() + 5000;
  setControls();
  renderStatus();
  try {
    await api(path, 'POST');
    notice = 'Befehl angenommen. Warte auf Bestätigung …';
    noticeUntil = Date.now() + 900;
    return true;
  } catch (error) {
    uiError = error.message;
    errorSource = 'action';
    noticeUntil = 0;
    return false;
  } finally {
    busy = false;
    setControls();
    renderStatus();
  }
}

async function selectPreset(number) {
  if (commandBlocked() || !Number.isInteger(number) || number < 0 || number >= presetCount) return;
  await sendAction(`/api/preset?number=${number}`);
}
async function submitPreset(event) {
  event.preventDefault();
  const value = el('preset-input').value.trim();
  const number = Number(value);
  const valid = /^\d+$/.test(value) && Number.isInteger(number) && number >= 0 && number < presetCount;
  el('preset-input').setAttribute('aria-invalid', String(!valid));
  el('preset-error').hidden = valid;
  if (!valid) {
    text(el('preset-error'), `Bitte eine Presetnummer zwischen 0 und ${presetCount - 1} eingeben.`);
    el('preset-input').focus();
    return;
  }
  if (!commandBlocked()) {
    inputEdited = false;
    await selectPreset(number);
  }
}

function presetLabel(preset) {
  if (preset.state === 0) return 'Name unbekannt';
  if (preset.state === 2) return 'Keine Antwort';
  return preset.name.trim() || 'Leeres Preset';
}
function isFavorite(number) { return Array.isArray(lastState?.favorites) && lastState.favorites.includes(number); }
async function toggleFavorite(number) {
  if (commandBlocked()) return;
  await sendAction(`/api/favorite/toggle?number=${number}`);
}
async function moveFavorite(slot, direction) {
  if (commandBlocked()) return;
  await sendAction(`/api/favorite/move?slot=${slot}&direction=${direction}`);
}
function updateFavoriteButtons() {
  for (const button of document.querySelectorAll('.favorite-toggle')) {
    const number = Number(button.dataset.number);
    const favorite = isFavorite(number);
    button.setAttribute('aria-pressed', String(favorite));
    button.setAttribute('aria-label', favorite ? `Preset ${number} aus Favoriten entfernen` : `Preset ${number} zu Favoriten hinzufügen`);
    button.textContent = favorite ? '★' : '☆';
  }
}
function renderFavorites() {
  const parent = el('favorites-grid');
  const favorites = Array.isArray(lastState?.favorites) ? lastState.favorites : [];
  const fragment = document.createDocumentFragment();
  favorites.forEach((number, slot) => {
    const card = document.createElement('div');
    card.className = 'favorite-slot';
    const main = document.createElement('div');
    main.className = 'favorite-slot-main';
    const position = document.createElement('span');
    position.className = 'favorite-slot-number';
    position.textContent = `${slot + 1}.`;
    const name = document.createElement('span');
    name.className = 'favorite-slot-name';
    const valid = Number.isInteger(number) && number >= 0 && number < presetCount;
    name.textContent = valid ? `${String(number).padStart(3, '0')} · ${presets[number] ? presetLabel(presets[number]) : 'Name unbekannt'}` : 'Nicht belegt';
    main.append(position, name);
    const actions = document.createElement('div');
    actions.className = 'favorite-slot-actions';
    for (const [label, direction] of [['←', -1], ['→', 1]]) {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = label;
      button.dataset.available = String(valid && slot + direction >= 0 && slot + direction < favorites.length);
      button.setAttribute('aria-label', `Favorit auf Platz ${slot + 1} ${direction < 0 ? 'nach links' : 'nach rechts'} verschieben`);
      button.onclick = () => moveFavorite(slot, direction);
      actions.appendChild(button);
    }
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.textContent = '×';
    remove.dataset.available = String(valid);
    remove.setAttribute('aria-label', `Favorit auf Platz ${slot + 1} entfernen`);
    remove.onclick = () => toggleFavorite(number);
    actions.appendChild(remove);
    card.append(main, actions);
    fragment.appendChild(card);
  });
  parent.replaceChildren(fragment);
  text(el('favorite-count'), `${favorites.filter(number => number >= 0).length} / ${favorites.length || 6}`);
  updateFavoriteButtons();
  setControls();
}
function searchKey(value) { return String(value).toLocaleLowerCase('de').normalize('NFD').replace(/[\u0300-\u036f]/g, ''); }
function renderPresetList(resetScroll = false) {
  const list = el('preset-list');
  const previousScroll = list.scrollTop;
  const focusedNumber = list.contains(document.activeElement) ? document.activeElement.dataset.number : undefined;
  const query = searchKey(el('preset-search').value.trim());
  const fragment = document.createDocumentFragment();
  let matches = 0;
  presets.forEach((preset, number) => {
    const padded = String(number).padStart(3, '0');
    const label = presetLabel(preset);
    if (query && !searchKey(`${padded} ${label}`).includes(query)) return;
    ++matches;
    const row = document.createElement('div');
    row.className = 'preset-row';
    row.dataset.number = String(number);
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'preset-select';
    button.dataset.number = String(number);
    button.setAttribute('aria-label', `Preset ${number}: ${label}`);
    for (const [className, value] of [['row-number', padded], ['row-name', label], ['row-state', '']]) {
      const span = document.createElement('span');
      span.className = className;
      span.textContent = value;
      button.appendChild(span);
    }
    button.onclick = () => selectPreset(number);
    const favorite = document.createElement('button');
    favorite.type = 'button';
    favorite.className = 'favorite-toggle';
    favorite.dataset.number = String(number);
    favorite.onclick = () => toggleFavorite(number);
    row.append(button, favorite);
    fragment.appendChild(row);
  });
  if (!matches) {
    const empty = document.createElement('p');
    empty.className = 'empty-state';
    empty.textContent = query ? 'Keine passenden Presets. Versuche einen anderen Namen oder eine Nummer.' : 'Noch keine Preset-Namen verfügbar.';
    fragment.appendChild(empty);
  }
  list.replaceChildren(fragment);
  list.setAttribute('aria-busy', 'false');
  text(el('preset-count'), query ? `${matches} Treffer` : `${presets.length} Presets`);
  updateActivePreset();
  updateFavoriteButtons();
  list.scrollTop = resetScroll ? 0 : previousScroll;
  if (focusedNumber !== undefined) list.querySelector(`[data-number="${focusedNumber}"]`)?.focus({preventScroll:true});
  setControls();
}
function updateActivePreset() {
  for (const row of document.querySelectorAll('.preset-row')) {
    const active = Number(row.dataset.number) === lastState?.presetNumber;
    row.setAttribute('aria-current', String(active));
    text(row.querySelector('.row-state'), active ? 'Aktiv' : '');
  }
}
function centerActivePreset() {
  if (el('preset-search').value.trim()) return;
  const list = el('preset-list');
  const row = list.querySelector('[aria-current="true"]');
  if (row && list.clientHeight) list.scrollTop += row.getBoundingClientRect().top - list.getBoundingClientRect().top - (list.clientHeight - row.offsetHeight) / 2;
}
async function refreshCache(revision) {
  if (cacheRevision === revision) return;
  const data = await api('/api/presets');
  if (!Array.isArray(data.presets)) throw new Error('Ungültige Presetliste empfangen.');
  const first = cacheRevision === -1;
  presets = data.presets;
  cacheRevision = data.revision;
  renderPresetList();
  renderFavorites();
  if (first) centerActivePreset();
}

function updateButtons(containerId, items, isScene) {
  const parent = el(containerId);
  if (!parent.firstElementChild?.matches('button')) parent.replaceChildren();
  items.forEach((item, index) => {
    let button = parent.children[index];
    if (!button) {
      button = document.createElement('button');
      button.type = 'button';
      const classes = isScene ? ['scene-number', 'scene-name', 'control-state'] : ['effect-code', 'effect-name', 'control-state'];
      for (const className of classes) {
        const span = document.createElement('span');
        span.className = className;
        button.appendChild(span);
      }
      parent.appendChild(button);
    }
    const available = isScene || item.available;
    button.dataset.available = String(available);
    button.className = `${isScene ? 'scene-button' : 'effect-button'} ${!available ? 'unavailable' : item.active ? 'on' : 'off'}`;
    button.setAttribute('aria-pressed', String(item.active && available));
    if (isScene) {
      const name = item.name.trim() || `Szene ${item.number}`;
      text(button.children[0], item.number);
      text(button.children[1], name);
      text(button.children[2], item.active ? 'Aktiv' : 'Inaktiv');
      button.setAttribute('aria-label', `Szene ${item.number}: ${name}`);
      button.onclick = () => { if (!commandBlocked() && lastState.ready) sendAction(`/api/scene?number=${item.number}`); };
    } else {
      const name = effectNames[item.label] || item.label;
      text(button.children[0], item.label);
      text(button.children[1], name);
      text(button.children[2], !available ? 'Nicht vorhanden' : item.active ? 'Aktiv' : 'Aus');
      button.setAttribute('aria-label', `${name}: ${!available ? 'Nicht vorhanden' : item.active ? 'Aktiv' : 'Aus'}`);
      button.onclick = () => { if (!commandBlocked() && lastState.ready && available) sendAction(`/api/effect?slot=${item.index}`); };
    }
  });
  while (parent.children.length > items.length) parent.lastElementChild.remove();
}

function midiDraftKey(slot, bank, index) { return `${slot}:${bank}:${index}`; }

function midiFormValues(form) {
  return {
    channel: form.elements.namedItem('channel').value,
    number: form.elements.namedItem('number').value,
    value: form.elements.namedItem('value').value
  };
}

function midiValuesMatch(left, right) {
  return !!right && left.channel !== '' && left.number !== '' && left.value !== '' &&
    Number(left.channel) === right.channel &&
    Number(left.number) === right.number && Number(left.value) === right.value;
}

function reconcileMidiDrafts(state) {
  let changed = false;
  for (const [key, draft] of customMidiDrafts) {
    if (!draft.pending) continue;
    const command = state.customMidi?.[draft.slot]?.banks?.[draft.bank]?.[draft.index];
    if (midiValuesMatch(draft.values, command)) customMidiDrafts.delete(key);
    else if (Date.now() - draft.submittedAt > 5000) draft.pending = false;
    else continue;
    changed = true;
  }
  if (changed) renderedCustomMidiRevision = -1;
}

function reconcileMidiNameDrafts(state) {
  let changed = false;
  for (const [slot, draft] of customMidiNameDrafts) {
    if (!draft.pending) continue;
    if (state.customMidi?.[slot]?.name === draft.value) customMidiNameDrafts.delete(slot);
    else if (Date.now() - draft.submittedAt > 5000) draft.pending = false;
    else continue;
    changed = true;
  }
  if (changed) renderedCustomMidiRevision = -1;
}

function midiEditor(slot, bank, index, command) {
  const key = midiDraftKey(slot, bank, index);
  const draft = customMidiDrafts.get(key);
  const values = draft?.values || command || {channel:1, number:0, value:0};
  const form = document.createElement('form');
  form.className = 'midi-command';
  const title = document.createElement('p');
  title.className = 'midi-bank-note';
  title.textContent = command ? `${command.channel} CC ${command.number} ${command.value}` : 'Noch kein CC-Befehl aktiv';
  const draftStatus = document.createElement('p');
  draftStatus.className = 'midi-draft-status';
  draftStatus.textContent = draft ? draft.pending ? 'Wird zum Testen übernommen …' : 'Noch nicht zum Testen übernommen' : '';
  const fields = document.createElement('div');
  fields.className = 'midi-fields';
  for (const [name, label, min, max, value] of [
    ['channel', 'Kanal', 1, 16, values.channel],
    ['cmd', 'Befehl', 0, 0, 'CC'],
    ['number', 'CC-Nr.', 0, 127, values.number],
    ['value', 'Wert', 0, 127, values.value]
  ]) {
    const wrapper = document.createElement('label');
    wrapper.textContent = label;
    const input = name === 'cmd' ? document.createElement('select') : document.createElement('input');
    input.name = name;
    if (name === 'cmd') {
      const option = document.createElement('option');
      option.value = 'CC';
      option.textContent = 'CC';
      input.appendChild(option);
    } else {
      input.type = 'number';
      input.min = min;
      input.max = max;
      input.step = 1;
      input.required = true;
      input.value = value;
      input.disabled = !!draft?.pending;
    }
    wrapper.appendChild(input);
    fields.appendChild(wrapper);
  }
  const actions = document.createElement('div');
  actions.className = 'midi-actions';
  const save = document.createElement('button');
  save.type = 'submit';
  save.textContent = 'Zum Testen übernehmen';
  save.dataset.pending = String(!!draft?.pending);
  actions.appendChild(save);
  if (command) {
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.textContent = 'Entfernen';
    remove.onclick = async () => {
      if (commandBlocked()) return;
      if (await sendAction(`/api/custom-midi/remove?slot=${slot}&bank=${bank}&index=${index}`)) {
        customMidiDrafts.delete(key);
      }
    };
    actions.appendChild(remove);
  }
  form.append(title, draftStatus, fields, actions);
  form.oninput = () => {
    const edited = midiFormValues(form);
    if (midiValuesMatch(edited, command)) customMidiDrafts.delete(key);
    else customMidiDrafts.set(key, {slot, bank, index, values:edited, pending:false});
    draftStatus.textContent = customMidiDrafts.has(key) ? 'Noch nicht zum Testen übernommen' : '';
    setControls();
  };
  form.onsubmit = async event => {
    event.preventDefault();
    if (commandBlocked() || !form.reportValidity()) return;
    const edited = midiFormValues(form);
    const channel = Number(edited.channel);
    const number = Number(edited.number);
    const value = Number(edited.value);
    if (![channel, number, value].every(Number.isInteger)) return;
    const pending = {slot, bank, index, values:edited, pending:true, submittedAt:Date.now()};
    customMidiDrafts.set(key, pending);
    draftStatus.textContent = 'Wird zum Testen übernommen …';
    save.dataset.pending = 'true';
    save.disabled = true;
    if (!await sendAction(`/api/custom-midi/command?slot=${slot}&bank=${bank}&index=${index}&channel=${channel}&number=${number}&value=${value}`)) {
      pending.pending = false;
      draftStatus.textContent = 'Noch nicht zum Testen übernommen';
      save.dataset.pending = 'false';
      setControls();
    }
  };
  return form;
}

function renderCustomMidi(state) {
  reconcileMidiDrafts(state);
  reconcileMidiNameDrafts(state);
  const draftCount = customMidiDrafts.size + customMidiNameDrafts.size;
  text(el('custom-midi-save-state'), draftCount
    ? `${draftCount} Änderung${draftCount === 1 ? '' : 'en'} noch zum Testen übernehmen.`
    : state.customMidiDirty
      ? 'Die getestete Belegung ist am Pedal aktiv, aber noch nicht dauerhaft gespeichert.'
      : 'Alle Custom-MIDI-Einstellungen sind dauerhaft gespeichert.');
  const slot = Number(el('custom-midi-slot').value);
  const config = state.customMidi?.[slot];
  if (!config) return;
  text(el('custom-midi-next'), config.mode === 'alternate'
    ? `Nächste Betätigung: Bank ${config.nextBank === 0 ? 'A' : 'B'}`
    : 'Jede Betätigung sendet Bank A.');
  if (renderedCustomMidiRevision === state.customMidiRevision && renderedCustomMidiSlot === slot) return;
  renderedCustomMidiRevision = state.customMidiRevision;
  renderedCustomMidiSlot = slot;
  el('custom-midi-mode').value = config.mode;
  el('custom-midi-name').value = customMidiNameDrafts.get(slot)?.value ?? config.name;
  el('custom-midi-name').placeholder = `MIDI ${slot + 1}`;
  text(el('custom-midi-name-note'), `Bis zu 20 Zeichen (ohne Umlaute). Leer lassen für „MIDI ${slot + 1}“.`);
  state.customMidi.forEach((setting, index) => {
    text(el('custom-midi-slot').options[index], `Fußtaster ${index + 1}${setting.name ? ` · ${setting.name}` : ''}`);
  });
  const banks = el('custom-midi-banks');
  banks.replaceChildren();
  config.banks.forEach((commands, bank) => {
    if (bank === 1 && config.mode !== 'alternate' && !customMidiDrafts.has(midiDraftKey(slot, bank, 0))) return;
    const panel = document.createElement('section');
    panel.className = 'midi-bank';
    const heading = document.createElement('h4');
    heading.textContent = `Bank ${bank === 0 ? 'A' : 'B'}${bank === 1 && config.mode !== 'alternate' ? ' (derzeit inaktiv)' : ''}`;
    panel.appendChild(heading);
    panel.appendChild(midiEditor(slot, bank, 0, commands[0] || null));
    banks.appendChild(panel);
  });
  setControls();
}

function renderState(state) {
  text(el('preset-number'), state.presetNumber >= 0 ? String(state.presetNumber).padStart(3, '0') : '—');
  text(el('preset'), state.presetNumber >= 0 ? state.presetName.trim() || 'Leeres Preset' : 'Warte auf MIDI');
  text(el('scene'), state.sceneNumber >= 1 ? `Szene ${state.sceneNumber} · ${state.sceneName.trim() || 'Ohne Namen'}` : 'Noch keine aktive Szene');
  const mode = {effects:'Effekte', scenes:'Szenen', presets:'Presets', customMidi:'Custom MIDI'}[state.mode] || '—';
  text(el('device-mode'), `Modus: ${mode}`);
  renderCustomMidi(state);
  const favoriteModeText = !state.favoriteModeEnabled ? 'Relativer Presetmodus ist aktiv.'
    : state.favoriteModeActive ? 'Die sechs Favoriten sind den Fußtastern fest zugeordnet.'
    : 'Favoritenmodus ist aktiviert, aber noch kein Favorit belegt. Der relative Modus bleibt aktiv.';
  text(el('favorite-mode-state'), favoriteModeText);
  const modeButton = el('favorite-mode-btn');
  modeButton.setAttribute('aria-pressed', String(!!state.favoriteModeEnabled));
  text(modeButton, state.favoriteModeEnabled ? 'Deaktivieren' : 'Aktivieren');
  if (renderedFavoritesRevision !== state.favoritesRevision) {
    renderedFavoritesRevision = state.favoritesRevision;
    renderFavorites();
  }
  text(el('scene-count'), `${state.sceneNumber >= 1 ? state.sceneNumber : '—'} / ${state.scenes.length}`);
  updateButtons('scenegrid', state.scenes, true);
  updateButtons('grid', state.slots, false);
  if (!inputEdited && document.activeElement !== el('preset-input') && state.presetNumber >= 0 && !state.scanning) el('preset-input').value = state.presetNumber;
  el('preset-input').placeholder = `0–${presetCount - 1}`;
  const changed = activePreset !== state.presetNumber;
  activePreset = state.presetNumber;
  updateActivePreset();
  if (changed && !state.scanning) centerActivePreset();
  text(el('save-state'), !state.storageReady ? 'Der Preset-Speicher ist nicht verfügbar.' : state.unsaved ? 'Neue oder geänderte Namen sind noch nicht gespeichert.' : 'Alle Preset-Namen sind auf dem Controller gespeichert.');
  el('scan-progress').hidden = !state.scanning;
  text(el('scan-count'), `${state.scanProgress} / ${presetCount}`);
  el('scan-meter').max = presetCount;
  el('scan-meter').value = Math.max(0, Math.min(presetCount, state.scanProgress));
  setControls();
  renderStatus();
}

async function loadStatus() {
  if (polling || busy) return;
  polling = true;
  const version = actionVersion;
  try {
    const state = await api('/api/status');
    if (version !== actionVersion) return;
    if (!Array.isArray(state.scenes) || !Array.isArray(state.slots) || !Array.isArray(state.favorites) ||
        !Array.isArray(state.customMidi) || state.customMidi.length !== 6 ||
        state.favorites.length !== 6 || !Number.isInteger(state.presetCount)) throw new Error('Ungültigen Gerätestatus empfangen.');
    lastState = state;
    presetCount = state.presetCount;
    online = true;
    if (!networkLoaded) loadNetworkSettings();
    if (errorSource === 'network') { uiError = ''; errorSource = ''; }
    renderState(state);
    try {
      await refreshCache(state.cacheRevision);
      if (errorSource === 'cache') { uiError = ''; errorSource = ''; renderStatus(); }
    } catch (error) {
      uiError = `Presetliste: ${error.message}`;
      errorSource = 'cache';
      el('preset-list').setAttribute('aria-busy', 'false');
      renderStatus();
    }
  } catch (error) {
    online = false;
    uiError = `Verbindung unterbrochen: ${error.message}`;
    errorSource = 'network';
    setControls();
    renderStatus();
  } finally { polling = false; }
}

async function startScan(type) {
  if (commandBlocked() || !lastState.ready) return;
  const message = type === 'deep'
    ? 'Alle Presets neu einlesen? Das Gerät wechselt dabei die Presets. Dauer etwa 4–13 Minuten. Anschließend wird das vorherige Preset wieder geladen.'
    : 'Unbekannte Presets einlesen? Währenddessen ist die Spielsteuerung gesperrt. Anschließend wird das vorherige Preset wieder geladen.';
  if (confirm(message)) await sendAction(`/api/scan/start?mode=${type}`);
}
el('preset-down').onclick = () => { if (!commandBlocked()) sendAction('/api/command?cmd=preset_down'); };
el('preset-up').onclick = () => { if (!commandBlocked()) sendAction('/api/command?cmd=preset_up'); };
el('save-flash-btn').onclick = () => sendAction('/api/save');
el('favorite-mode-btn').onclick = () => {
  if (!commandBlocked()) sendAction(`/api/favorite/mode?enabled=${lastState.favoriteModeEnabled ? 0 : 1}`);
};
el('scan-smart-btn').onclick = () => startScan('smart');
el('scan-deep-btn').onclick = () => startScan('deep');
el('scan-stop-btn').onclick = () => sendAction('/api/scan/stop');
el('retry-btn').onclick = () => { uiError = ''; errorSource = ''; noticeUntil = 0; cacheRevision = -1; loadStatus(); };
el('network-form').onsubmit = saveNetwork;
for (const input of document.querySelectorAll('input[name="network-mode"]')) input.onchange = updateNetworkFields;
el('network-dhcp').onchange = updateNetworkFields;
for (let slot = 0; slot < 6; ++slot) {
  const option = document.createElement('option');
  option.value = String(slot);
  option.textContent = `Fußtaster ${slot + 1}`;
  el('custom-midi-slot').appendChild(option);
}
el('custom-midi-slot').onchange = () => { if (lastState) renderCustomMidi(lastState); };
el('custom-midi-name').oninput = () => {
  const slot = Number(el('custom-midi-slot').value);
  const value = el('custom-midi-name').value;
  if (value.trim() === lastState?.customMidi?.[slot]?.name) customMidiNameDrafts.delete(slot);
  else customMidiNameDrafts.set(slot, {value, pending:false});
  el('custom-midi-name').setCustomValidity('');
  setControls();
};
el('custom-midi-name-form').onsubmit = async event => {
  event.preventDefault();
  if (commandBlocked()) return;
  const slot = Number(el('custom-midi-slot').value);
  const input = el('custom-midi-name');
  const value = input.value.trim();
  input.setCustomValidity(/^[\x20-\x7E]{0,20}$/.test(value) ? '' : 'Bitte nur Zeichen ohne Umlaute eingeben.');
  if (!input.reportValidity()) return;
  const draft = {value, pending:true, submittedAt:Date.now()};
  customMidiNameDrafts.set(slot, draft);
  input.value = value;
  setControls();
  if (!await sendAction(`/api/custom-midi/name?slot=${slot}&name=${encodeURIComponent(value)}`)) {
    draft.pending = false;
    setControls();
  }
};
el('custom-midi-mode').onchange = () => {
  if (!commandBlocked()) sendAction(`/api/custom-midi/mode?slot=${el('custom-midi-slot').value}&mode=${el('custom-midi-mode').value === 'alternate' ? 1 : 0}`);
};
el('custom-midi-save').onclick = () => {
  if (!commandBlocked() && lastState?.customMidiDirty && !customMidiDrafts.size)
    sendAction('/api/custom-midi/save');
};
el('preset-search').oninput = () => renderPresetList(true);
el('preset-input').oninput = () => {
  inputEdited = true;
  el('preset-input').removeAttribute('aria-invalid');
  el('preset-error').hidden = true;
};
el('direct-choice').noValidate = true;
el('direct-choice').onsubmit = submitPreset;
addEventListener('hashchange', setView);
setView();
setControls();
loadStatus();
setInterval(loadStatus, 500);
