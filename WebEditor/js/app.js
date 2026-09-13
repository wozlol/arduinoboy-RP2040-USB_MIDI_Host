import { ArduinoboyLink } from './sysex.js';

const link = new ArduinoboyLink();

const els = {
  portsCard: document.querySelector('.ports-card'),
  midiInSelect: document.getElementById('midiInSelect'),
  midiOutSelect: document.getElementById('midiOutSelect'),
  connectBtn: document.getElementById('connectBtn'),
  connectionPill: document.getElementById('connectionPill'),
  connectionLabel: document.getElementById('connectionLabel'),
  loadFromDeviceBtn: document.getElementById('loadFromDeviceBtn'),
  saveToDeviceBtn: document.getElementById('saveToDeviceBtn'),
  restoreDefaultsBtn: document.getElementById('restoreDefaultsBtn'),
  downloadPresetBtn: document.getElementById('downloadPresetBtn'),
  uploadPresetInput: document.getElementById('uploadPresetInput'),
  settingsRoot: document.getElementById('settingsRoot'),
  busyOverlay: document.getElementById('busyOverlay'),
  busyText: document.getElementById('busyText'),
  toast: document.getElementById('toast'),
  modeSelect: document.getElementById('modeSelect'),
  forceModeCheck: document.getElementById('forceModeCheck'),
  slaveChSelect: document.getElementById('slaveChSelect'),
  masterChSelect: document.getElementById('masterChSelect'),
  keyboardChSelect: document.getElementById('keyboardChSelect'),
  keyboardCompatCheck: document.getElementById('keyboardCompatCheck'),
  livemapChSelect: document.getElementById('livemapChSelect'),
  midioutTable: document.querySelector('#midioutTable tbody'),
  bitDelayInput: document.getElementById('bitDelayInput'),
  bitMultInput: document.getElementById('bitMultInput')
};

let dirty = false;
let toastTimer = null;
// keyboardChToInst and midioutByteDelay are real bytes in the firmware's memory layout,
// but nothing in any Arduinoboy firmware build actually reads either one, and the
// original Max editor has no controls for them - so this tool doesn't show any either.
// Whatever values are loaded from the device are just passed through unchanged on save,
// so saving here can't corrupt them.
let loadedKeyboardChToInst = false;
let loadedMidioutByteDelay = [0, 0];

function populateChannelSelect(select) {
  select.innerHTML = '';
  for (let ch = 0; ch < 16; ch++) {
    const opt = document.createElement('option');
    opt.value = String(ch);
    opt.textContent = String(ch + 1);
    select.appendChild(opt);
  }
}

const MIDIOUT_ROW_LABELS = ['PU1', 'PU2', 'WAV', 'NOI'];

function buildMidioutTable() {
  els.midioutTable.innerHTML = '';
  MIDIOUT_ROW_LABELS.forEach((label, rowIndex) => {
    const tr = document.createElement('tr');

    const nameTd = document.createElement('td');
    nameTd.textContent = label;
    tr.appendChild(nameTd);

    const noteChTd = document.createElement('td');
    const noteChSelect = document.createElement('select');
    noteChSelect.className = 'ch-select';
    noteChSelect.dataset.row = String(rowIndex);
    noteChSelect.dataset.field = 'midioutNoteCh';
    populateChannelSelect(noteChSelect);
    noteChSelect.addEventListener('change', markDirty);
    noteChTd.appendChild(noteChSelect);
    tr.appendChild(noteChTd);

    const ccChTd = document.createElement('td');
    const ccChSelect = document.createElement('select');
    ccChSelect.className = 'ch-select';
    ccChSelect.dataset.row = String(rowIndex);
    ccChSelect.dataset.field = 'midioutCcCh';
    populateChannelSelect(ccChSelect);
    ccChSelect.addEventListener('change', markDirty);
    ccChTd.appendChild(ccChSelect);
    tr.appendChild(ccChTd);

    for (let slot = 0; slot < 7; slot++) {
      const td = document.createElement('td');
      const input = document.createElement('input');
      input.type = 'number';
      input.min = '0';
      input.max = '127';
      input.dataset.row = String(rowIndex);
      input.dataset.slot = String(slot);
      input.dataset.field = 'midioutCcNumbers';
      input.addEventListener('change', markDirty);
      td.appendChild(input);
      tr.appendChild(td);
    }

    const modeTd = document.createElement('td');
    const modeSelect = document.createElement('select');
    modeSelect.dataset.row = String(rowIndex);
    modeSelect.dataset.field = 'midioutCcMode';
    modeSelect.innerHTML = '<option value="1">Multiple CCs</option><option value="0">Single CC (scaled)</option>';
    modeSelect.addEventListener('change', markDirty);
    modeTd.appendChild(modeSelect);
    tr.appendChild(modeTd);

    const scalingTd = document.createElement('td');
    const scalingSelect = document.createElement('select');
    scalingSelect.dataset.row = String(rowIndex);
    scalingSelect.dataset.field = 'midioutCcScaling';
    scalingSelect.innerHTML = '<option value="0">Use Exact Value</option><option value="1">Scale to Full Range</option>';
    scalingSelect.addEventListener('change', markDirty);
    scalingTd.appendChild(scalingSelect);
    tr.appendChild(scalingTd);

    els.midioutTable.appendChild(tr);
  });
}

function markDirty() {
  dirty = true;
  els.saveToDeviceBtn.classList.add('has-unsaved');
}

function clearDirty() {
  dirty = false;
  els.saveToDeviceBtn.classList.remove('has-unsaved');
}

function showToast(message, kind) {
  clearTimeout(toastTimer);
  els.toast.textContent = message;
  els.toast.className = 'toast show' + (kind ? ' ' + kind : '');
  toastTimer = setTimeout(() => { els.toast.className = 'toast'; }, 3200);
}

function showBusy(text) {
  els.busyText.textContent = text;
  els.busyOverlay.hidden = false;
}
function hideBusy() { els.busyOverlay.hidden = true; }

function fillSettingsIntoForm(s) {
  els.modeSelect.value = String(s.mode);
  els.forceModeCheck.checked = s.forceMode;
  els.slaveChSelect.value = String(s.lsdjSlaveCh);
  els.masterChSelect.value = String(s.lsdjMasterCh);
  els.keyboardChSelect.value = String(s.keyboardCh);
  els.keyboardCompatCheck.checked = s.keyboardCompatMode;
  loadedKeyboardChToInst = s.keyboardChToInst;
  els.livemapChSelect.value = String(s.livemapCh);
  for (let i = 0; i < 5; i++) document.getElementById('mgbCh' + i).value = String(s.mgbCh[i]);

  els.midioutTable.querySelectorAll('select[data-field="midioutNoteCh"]').forEach((el) => {
    el.value = String(s.midioutNoteCh[Number(el.dataset.row)]);
  });
  els.midioutTable.querySelectorAll('select[data-field="midioutCcCh"]').forEach((el) => {
    el.value = String(s.midioutCcCh[Number(el.dataset.row)]);
  });
  els.midioutTable.querySelectorAll('select[data-field="midioutCcMode"]').forEach((el) => {
    el.value = s.midioutCcMode[Number(el.dataset.row)] ? '1' : '0';
  });
  els.midioutTable.querySelectorAll('select[data-field="midioutCcScaling"]').forEach((el) => {
    el.value = s.midioutCcScaling[Number(el.dataset.row)] ? '1' : '0';
  });
  els.midioutTable.querySelectorAll('input[data-field="midioutCcNumbers"]').forEach((el) => {
    const row = Number(el.dataset.row);
    const slot = Number(el.dataset.slot);
    el.value = String(s.midioutCcNumbers[row][slot]);
  });

  els.bitDelayInput.value = String(s.midioutBitDelay[0]);
  els.bitMultInput.value = String(s.midioutBitDelay[1]);
  loadedMidioutByteDelay = s.midioutByteDelay;

  clearDirty();
}

function readSettingsFromForm() {
  const midioutNoteCh = [0, 0, 0, 0];
  const midioutCcCh = [0, 0, 0, 0];
  const midioutCcMode = [false, false, false, false];
  const midioutCcScaling = [false, false, false, false];
  const midioutCcNumbers = [[0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0]];

  els.midioutTable.querySelectorAll('select[data-field="midioutNoteCh"]').forEach((el) => {
    midioutNoteCh[Number(el.dataset.row)] = Number(el.value);
  });
  els.midioutTable.querySelectorAll('select[data-field="midioutCcCh"]').forEach((el) => {
    midioutCcCh[Number(el.dataset.row)] = Number(el.value);
  });
  els.midioutTable.querySelectorAll('select[data-field="midioutCcMode"]').forEach((el) => {
    midioutCcMode[Number(el.dataset.row)] = el.value === '1';
  });
  els.midioutTable.querySelectorAll('select[data-field="midioutCcScaling"]').forEach((el) => {
    midioutCcScaling[Number(el.dataset.row)] = el.value === '1';
  });
  els.midioutTable.querySelectorAll('input[data-field="midioutCcNumbers"]').forEach((el) => {
    const row = Number(el.dataset.row);
    const slot = Number(el.dataset.slot);
    midioutCcNumbers[row][slot] = clamp7(el.value);
  });

  return {
    mode: Number(els.modeSelect.value),
    forceMode: els.forceModeCheck.checked,
    lsdjSlaveCh: Number(els.slaveChSelect.value),
    lsdjMasterCh: Number(els.masterChSelect.value),
    keyboardCh: Number(els.keyboardChSelect.value),
    keyboardCompatMode: els.keyboardCompatCheck.checked,
    keyboardChToInst: loadedKeyboardChToInst,
    livemapCh: Number(els.livemapChSelect.value),
    mgbCh: [0, 1, 2, 3, 4].map((i) => Number(document.getElementById('mgbCh' + i).value)),
    midioutNoteCh,
    midioutCcCh,
    midioutCcMode,
    midioutCcScaling,
    midioutCcNumbers,
    midioutBitDelay: [clamp7(els.bitDelayInput.value), clamp7(els.bitMultInput.value)],
    midioutByteDelay: loadedMidioutByteDelay
  };
}

function clamp7(v) {
  const n = Math.round(Number(v)) || 0;
  return Math.max(0, Math.min(127, n));
}

function refreshPortLists() {
  const fillSelect = (select, ports) => {
    const prev = select.value;
    select.innerHTML = '<option value="">-- select --</option>';
    ports.forEach((p) => {
      const opt = document.createElement('option');
      opt.value = p.id;
      opt.textContent = p.name;
      select.appendChild(opt);
    });
    if (ports.some((p) => p.id === prev)) select.value = prev;
  };
  fillSelect(els.midiInSelect, link.listInputs());
  fillSelect(els.midiOutSelect, link.listOutputs());
  maybeAutoConnect();
}

// The Arduinoboy's own USB descriptor names it "Game Boy" (see UsbMidiInit() in the
// firmware). If a port with that exact name is available on both sides and nothing else
// is going on, just connect to it - no reason to make someone click through the obvious
// choice every time. Manual port selection still works for anyone whose board enumerates
// under a different name, or who wants a different device.
let userDisconnected = false;

function maybeAutoConnect() {
  if (userDisconnected || link.connected || link.handshaking) return;
  const inOpt = Array.from(els.midiInSelect.options).find((o) => o.textContent === 'Game Boy');
  const outOpt = Array.from(els.midiOutSelect.options).find((o) => o.textContent === 'Game Boy');
  if (!inOpt || !outOpt) return;
  els.midiInSelect.value = inOpt.value;
  els.midiOutSelect.value = outOpt.value;
  attemptConnect();
}

function attemptConnect() {
  const inputId = els.midiInSelect.value;
  const outputId = els.midiOutSelect.value;
  if (!inputId || !outputId) {
    showToast('Select both a MIDI in port and a MIDI out port first.', 'error');
    return;
  }
  userDisconnected = false;
  pendingAction = 'connect';
  link.connect(inputId, outputId);
}

function attemptDisconnect() {
  userDisconnected = true;
  link.disconnect();
}

function setConnectionPill(state) {
  els.connectionPill.className = 'connection-pill ' + state;
  const labels = {
    disconnected: 'Not connected',
    connecting: 'Connecting…',
    connected: 'Connected',
    unsupported: 'Web MIDI not supported',
    denied: 'MIDI access denied'
  };
  els.connectionLabel.textContent = labels[state] || state;
}

let pendingAction = null; // 'connect' | 'save' | 'restore' | null

link.addEventListener('status', (e) => {
  const { state, error } = e.detail;
  setConnectionPill(state);
  const connected = state === 'connected';
  els.loadFromDeviceBtn.disabled = !connected;
  els.saveToDeviceBtn.disabled = !connected;
  els.restoreDefaultsBtn.disabled = !connected;
  els.connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
  els.settingsRoot.hidden = !connected;
  els.portsCard.classList.toggle('needs-connection', !connected);

  if (state === 'connecting') {
    showBusy('Connecting to Arduinoboy…');
  } else if (state === 'connected') {
    hideBusy();
  } else if (state === 'disconnected') {
    hideBusy();
    if (error && pendingAction === 'connect') showToast(error, 'error');
    pendingAction = null;
  } else if (state === 'unsupported') {
    showToast('This browser does not support Web MIDI. Use Chrome, Edge, Brave, or Opera.', 'error');
  } else if (state === 'denied') {
    showToast('MIDI access was denied. Reload the page and allow it to use System Exclusive messages.', 'error');
  }
});

link.addEventListener('settings', (e) => {
  fillSettingsIntoForm(e.detail.settings);
  hideBusy();
  if (pendingAction === 'save') showToast('Saved to device.', 'success');
  else if (pendingAction === 'restore') showToast('Restored factory defaults.', 'success');
  else showToast('Loaded settings from device.', 'success');
  pendingAction = null;
});

link.addEventListener('ports-changed', refreshPortLists);

els.connectBtn.addEventListener('click', () => {
  if (link.connected || link.handshaking) attemptDisconnect();
  else attemptConnect();
});

els.connectionPill.addEventListener('click', () => {
  if (link.connected || link.handshaking) attemptDisconnect();
});

els.loadFromDeviceBtn.addEventListener('click', () => {
  if (dirty && !confirm('Discard unsaved changes and reload from the device?')) return;
  showBusy('Reloading from device…');
  link.reloadFromDevice();
});

els.saveToDeviceBtn.addEventListener('click', () => {
  pendingAction = 'save';
  showBusy('Saving to device…');
  link.saveToDevice(readSettingsFromForm());
});

els.restoreDefaultsBtn.addEventListener('click', () => {
  if (!confirm('Restore factory defaults? This overwrites all settings currently stored on the device.')) return;
  pendingAction = 'restore';
  showBusy('Restoring factory defaults…');
  link.restoreDefaults();
});

els.downloadPresetBtn.addEventListener('click', () => {
  const settings = link.settings || readSettingsFromForm();
  const blob = new Blob([JSON.stringify(settings, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'arduinoboy-preset.json';
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
});

els.uploadPresetInput.addEventListener('change', async () => {
  const file = els.uploadPresetInput.files[0];
  els.uploadPresetInput.value = '';
  if (!file) return;
  try {
    const settings = JSON.parse(await file.text());
    fillSettingsIntoForm(settings);
    markDirty();
    els.settingsRoot.hidden = false;
    showToast('Preset loaded into the form. Connect and Save to Device to write it to a device.', 'success');
  } catch (err) {
    showToast('Could not read that preset file.', 'error');
  }
});

buildMidioutTable();
[els.slaveChSelect, els.masterChSelect, els.keyboardChSelect, els.livemapChSelect].forEach(populateChannelSelect);
for (let i = 0; i < 5; i++) populateChannelSelect(document.getElementById('mgbCh' + i));

[
  els.modeSelect, els.forceModeCheck, els.slaveChSelect, els.masterChSelect, els.keyboardChSelect,
  els.keyboardCompatCheck, els.livemapChSelect,
  els.bitDelayInput, els.bitMultInput
].forEach((el) => el.addEventListener('change', markDirty));
for (let i = 0; i < 5; i++) document.getElementById('mgbCh' + i).addEventListener('change', markDirty);

link.init();
