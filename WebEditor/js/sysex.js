// Web MIDI + SysEx transport for the Arduinoboy "Programmer Mode" protocol.
// This is the exact wire protocol implemented in Mode_Programmer.ino, shared
// unchanged across the original trash80/Entropy Electronics firmware and every
// AVR/Teensy/RP2040 fork descended from it - so this file targets any
// Arduinoboy that runs that file, not a specific board or fork.
//
// Framing: every message is `F0 69 <cmd> <payload bytes...> <checksum> F7`.
// The checksum mirrors the firmware's own (inert - it never actually rejects a
// mismatch) formula for wire-format compatibility, not because anything
// currently verifies it. Every outgoing payload is padded to at least 1 byte
// even when the command needs none, because the firmware's checksum routine
// has a genuine off-by-one bug that free-spins forever on a truly empty
// payload (see buildMessage below) - this file never sends that shape.

export const MFR_ID = 0x69;
export const MEM_MAX = 65;

// Command bytes. CONNECT_REQUEST and SETTINGS_DUMP are both numerically 64 in
// the firmware - they're never ambiguous because one only ever arrives from
// the editor and the other only ever arrives from the device.
export const CMD = {
  BEACON: 0x7F,          // device -> editor, periodic "I'm in programmer mode" ping
  CONNECT_REQUEST: 64,   // editor -> device
  CONNECT_ACK: 65,       // device -> editor, no payload
  CONNECT_CONFIRM: 66,   // editor -> device
  SETTINGS_DUMP: 0x40,   // device -> editor, full 65-byte memory image
  SAVE_MEMORY: 70,       // editor -> device, 61 bytes (memory[4..64])
  RESTORE_DEFAULTS: 71,  // editor -> device
  ENTER_PROGRAMMER: 72,  // editor -> device, works from any mode at any time
  GET_MODE: 73,          // editor -> device
  SET_MODE: 74,          // editor -> device, 1 byte
  SET_MIDIOUT_DELAY: 75  // editor -> device, 4 bytes
};

// The device's own reply to GET_MODE isn't a normal `cmd + payload` message -
// it's literally `F0 69 <modeNumber> F7`, reusing the command-byte position to
// carry the raw mode (0-6). None of the real command bytes above fall in that
// range, so it's never ambiguous.
const MODE_REPLY_MAX = 6;

const CONNECT_BEACON_TIMEOUT_MS = 2500;   // device beacons at most once/second while waiting
const CONNECTED_KEEPALIVE_MS = 800;       // device drops the session after 2000ms of silence
const HANDSHAKE_STEP_TIMEOUT_MS = 1500;

/** Field layout of the 65-byte memory image. Offsets match Mode_Programmer.ino / Arduinoboy.ino exactly. */
export const FIELDS = {
  forceMode: 4,
  mode: 5,
  lsdjSlaveCh: 6,
  lsdjMasterCh: 7,
  keyboardCh: 8,
  keyboardCompatMode: 9,
  keyboardChToInst: 10, // defined by the firmware but not read anywhere in it - exposed for completeness only
  midioutNoteCh: 11,    // 4: pu1, pu2, wav, noi
  midioutCcCh: 15,      // 4
  midioutCcMode: 19,    // 4 (boolean-ish per channel)
  midioutCcScaling: 23, // 4 (boolean-ish per channel)
  midioutCcNumbers: 27, // 28: 4 channels x 7 CC slots
  mgbCh: 55,            // 5: pu1, pu2, wav, noi, poly
  livemapCh: 60,
  midioutBitDelay: 61,  // 2: value, multiplier
  midioutByteDelay: 63  // 2: value, multiplier
};

export function computeChecksum(bytes) {
  // Mirrors byte (uint8_t) wraparound from the firmware's own checkdata += ...
  // loop, including its single conditional -0x7F reduction. Not a real CRC -
  // just reproducing exactly what the device would compute if it ever started
  // checking it.
  let sum = 0;
  for (const b of bytes) sum = (sum + b) & 0xFF;
  if (sum & 0x80) sum -= 0x7F;
  return sum & 0x7F;
}

export function buildMessage(cmd, payload = []) {
  const body = payload.length > 0 ? payload : [0];
  return [0xF0, MFR_ID, cmd, ...body, computeChecksum(body), 0xF7];
}

/** Raw 65-byte memory image -> a flat settings object keyed by FIELDS names. */
export function bytesToSettings(mem) {
  const readN = (offset, n) => Array.from(mem.slice(offset, offset + n));
  return {
    forceMode: !!mem[FIELDS.forceMode],
    mode: mem[FIELDS.mode],
    lsdjSlaveCh: mem[FIELDS.lsdjSlaveCh],
    lsdjMasterCh: mem[FIELDS.lsdjMasterCh],
    keyboardCh: mem[FIELDS.keyboardCh],
    keyboardCompatMode: !!mem[FIELDS.keyboardCompatMode],
    keyboardChToInst: !!mem[FIELDS.keyboardChToInst],
    midioutNoteCh: readN(FIELDS.midioutNoteCh, 4),
    midioutCcCh: readN(FIELDS.midioutCcCh, 4),
    midioutCcMode: readN(FIELDS.midioutCcMode, 4).map(Boolean),
    midioutCcScaling: readN(FIELDS.midioutCcScaling, 4).map(Boolean),
    // 4 channels x 7 CC numbers, row-major: midioutCcNumbers[channel][slot]
    midioutCcNumbers: [0, 1, 2, 3].map((ch) => readN(FIELDS.midioutCcNumbers + ch * 7, 7)),
    mgbCh: readN(FIELDS.mgbCh, 5),
    livemapCh: mem[FIELDS.livemapCh],
    midioutBitDelay: readN(FIELDS.midioutBitDelay, 2),
    midioutByteDelay: readN(FIELDS.midioutByteDelay, 2)
  };
}

/** Settings object -> the 61-byte payload the firmware expects for SAVE_MEMORY (memory[4..64]). */
export function settingsToSaveBytes(s) {
  const mem = new Array(MEM_MAX).fill(0);
  mem[FIELDS.forceMode] = s.forceMode ? 1 : 0;
  mem[FIELDS.mode] = s.mode & 0x7F;
  mem[FIELDS.lsdjSlaveCh] = s.lsdjSlaveCh & 0x0F;
  mem[FIELDS.lsdjMasterCh] = s.lsdjMasterCh & 0x0F;
  mem[FIELDS.keyboardCh] = s.keyboardCh & 0x0F;
  mem[FIELDS.keyboardCompatMode] = s.keyboardCompatMode ? 1 : 0;
  mem[FIELDS.keyboardChToInst] = s.keyboardChToInst ? 1 : 0;
  s.midioutNoteCh.forEach((v, i) => { mem[FIELDS.midioutNoteCh + i] = v & 0x0F; });
  s.midioutCcCh.forEach((v, i) => { mem[FIELDS.midioutCcCh + i] = v & 0x0F; });
  s.midioutCcMode.forEach((v, i) => { mem[FIELDS.midioutCcMode + i] = v ? 1 : 0; });
  s.midioutCcScaling.forEach((v, i) => { mem[FIELDS.midioutCcScaling + i] = v ? 1 : 0; });
  s.midioutCcNumbers.forEach((row, ch) => {
    row.forEach((v, slot) => { mem[FIELDS.midioutCcNumbers + ch * 7 + slot] = v & 0x7F; });
  });
  s.mgbCh.forEach((v, i) => { mem[FIELDS.mgbCh + i] = v & 0x0F; });
  mem[FIELDS.livemapCh] = s.livemapCh & 0x0F;
  mem[FIELDS.midioutBitDelay] = s.midioutBitDelay[0] & 0x7F;
  mem[FIELDS.midioutBitDelay + 1] = s.midioutBitDelay[1] & 0x7F;
  mem[FIELDS.midioutByteDelay] = s.midioutByteDelay[0] & 0x7F;
  mem[FIELDS.midioutByteDelay + 1] = s.midioutByteDelay[1] & 0x7F;
  // memory[4..64] inclusive = 61 bytes
  return mem.slice(4, MEM_MAX);
}

export class ArduinoboyLink extends EventTarget {
  constructor() {
    super();
    this.access = null;
    this.input = null;
    this.output = null;
    this.connected = false;
    this.handshaking = false;
    this.settings = null;
    this.deviceVersion = null;
    this._timers = [];
    this._lastConnectSentAt = 0;
    this._keepaliveTimer = null;
    this._watchdogTimer = null;
  }

  emit(name, detail) { this.dispatchEvent(new CustomEvent(name, { detail })); }

  async init() {
    if (!navigator.requestMIDIAccess) {
      this.emit('status', { state: 'unsupported' });
      return;
    }
    try {
      this.access = await navigator.requestMIDIAccess({ sysex: true });
    } catch (err) {
      this.emit('status', { state: 'denied' });
      return;
    }
    this.access.onstatechange = () => this.emit('ports-changed', {});
    this.emit('ports-changed', {});
  }

  listInputs() {
    if (!this.access) return [];
    return Array.from(this.access.inputs.values()).map((p) => ({ id: p.id, name: p.name }));
  }

  listOutputs() {
    if (!this.access) return [];
    return Array.from(this.access.outputs.values()).map((p) => ({ id: p.id, name: p.name }));
  }

  disconnect() {
    this._clearTimers();
    if (this.input) this.input.onmidimessage = null;
    this.input = null;
    this.output = null;
    this.connected = false;
    this.handshaking = false;
    this.emit('status', { state: 'disconnected' });
  }

  _clearTimers() {
    for (const t of this._timers) clearTimeout(t);
    this._timers = [];
    if (this._keepaliveTimer) clearInterval(this._keepaliveTimer);
    this._keepaliveTimer = null;
    if (this._watchdogTimer) clearTimeout(this._watchdogTimer);
    this._watchdogTimer = null;
  }

  /** Selects ports and starts the handshake: send ENTER_PROGRAMMER, then wait for the device's beacon. */
  connect(inputId, outputId) {
    if (!this.access) return;
    const input = this.access.inputs.get(inputId);
    const output = this.access.outputs.get(outputId);
    if (!input || !output) {
      this.emit('status', { state: 'disconnected', error: 'port-not-found' });
      return;
    }
    this._clearTimers();
    this.input = input;
    this.output = output;
    this._lastInputId = inputId;
    this._lastOutputId = outputId;
    this.connected = false;
    this.handshaking = true;
    this.settings = null;
    this.input.onmidimessage = (e) => this._onMessage(e);
    this.emit('status', { state: 'connecting' });
    this._sendRaw(CMD.ENTER_PROGRAMMER, []);
    this._armWatchdog('no response from the device. Check the port selection, and on older RP2040 firmware, try switching the Arduinoboy to Keyboard or mGB mode first - some builds only read USB sysex in those two modes.');
  }

  /**
   * There's no "just resend the dump" command that's free of side effects once already
   * connected - CONNECT_CONFIRM only triggers one on the *first* connect, and both commands
   * that always resend it (SAVE_MEMORY, RESTORE_DEFAULTS) mutate device memory as a side
   * effect. So a clean reload means letting the device's own 2-second inactivity timeout
   * drop the session (which also correctly unwinds its side of the connection, avoiding the
   * stack growth a redundant ENTER_PROGRAMMER while already inside it would risk), then
   * running the full handshake again from scratch.
   */
  reloadFromDevice() {
    if (!this._lastInputId || !this._lastOutputId) return;
    const inputId = this._lastInputId;
    const outputId = this._lastOutputId;
    this._clearTimers();
    if (this.input) this.input.onmidimessage = null;
    this.input = null;
    this.output = null;
    this.connected = false;
    this.handshaking = false;
    setTimeout(() => this.connect(inputId, outputId), 2200);
  }

  _armWatchdog(message) {
    if (this._watchdogTimer) clearTimeout(this._watchdogTimer);
    this._watchdogTimer = setTimeout(() => {
      if (this.connected) return; // only fires while still trying to connect
      this.disconnect();
      this.emit('status', { state: 'disconnected', error: message });
    }, CONNECT_BEACON_TIMEOUT_MS + HANDSHAKE_STEP_TIMEOUT_MS * 2);
  }

  _sendRaw(cmd, payload) {
    if (!this.output) return;
    const msg = buildMessage(cmd, payload);
    if (window.ARDUINOBOY_DEBUG) console.log('[arduinoboy tx]', msg.map((b) => b.toString(16).padStart(2, '0')).join(' '));
    this.output.send(msg);
  }

  _onMessage(e) {
    const data = e.data;
    if (window.ARDUINOBOY_DEBUG) console.log('[arduinoboy rx]', Array.from(data).map((b) => b.toString(16).padStart(2, '0')).join(' '));
    if (data.length < 4 || data[0] !== 0xF0 || data[1] !== MFR_ID || data[data.length - 1] !== 0xF7) return;
    const cmd = data[2];

    // GET_MODE's reply reuses the command-byte slot to carry the raw mode number.
    if (cmd <= MODE_REPLY_MAX && data.length === 4) {
      this.emit('mode', { mode: cmd });
      return;
    }

    switch (cmd) {
      case CMD.BEACON: {
        if (this.connected) return; // already connected, ignore further beacons
        const verFirst = data[3];
        const verSecond = data[4];
        this.deviceVersion = [verFirst, verSecond];
        this._sendRaw(CMD.CONNECT_REQUEST, [verFirst, verSecond]);
        break;
      }
      case CMD.CONNECT_ACK: {
        if (this.connected || !this.deviceVersion) return;
        this._sendRaw(CMD.CONNECT_CONFIRM, this.deviceVersion);
        break;
      }
      case CMD.SETTINGS_DUMP: {
        // data: F0 69 40 <65 bytes> F7
        const mem = data.slice(3, 3 + MEM_MAX);
        this.settings = bytesToSettings(mem);
        const wasConnected = this.connected;
        this.connected = true;
        this.handshaking = false;
        if (this._watchdogTimer) { clearTimeout(this._watchdogTimer); this._watchdogTimer = null; }
        if (!wasConnected) {
          this.emit('status', { state: 'connected', deviceVersion: this.deviceVersion });
          this._startKeepalive();
        }
        this.emit('settings', { settings: this.settings });
        break;
      }
      default:
        break;
    }
  }

  _startKeepalive() {
    if (this._keepaliveTimer) clearInterval(this._keepaliveTimer);
    this._keepaliveTimer = setInterval(() => {
      if (!this.connected || !this.deviceVersion) return;
      this._sendRaw(CMD.CONNECT_CONFIRM, this.deviceVersion);
    }, CONNECTED_KEEPALIVE_MS);
  }

  saveToDevice(settings) {
    if (!this.connected) return;
    this._sendRaw(CMD.SAVE_MEMORY, settingsToSaveBytes(settings));
  }

  restoreDefaults() {
    if (!this.connected) return;
    this._sendRaw(CMD.RESTORE_DEFAULTS, []);
  }

  requestMode() {
    if (!this.connected) return;
    this._sendRaw(CMD.GET_MODE, []);
  }

  setModeRemote(mode) {
    if (!this.connected) return;
    this._sendRaw(CMD.SET_MODE, [mode & 0x7F]);
  }

  setMidioutDelay(bitDelay, bitMult, byteDelay, byteMult) {
    if (!this.connected) return;
    this._sendRaw(CMD.SET_MIDIOUT_DELAY, [bitDelay & 0x7F, bitMult & 0x7F, byteDelay & 0x7F, byteMult & 0x7F]);
  }
}
