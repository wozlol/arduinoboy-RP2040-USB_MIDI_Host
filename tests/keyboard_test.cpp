#include <cassert>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <iostream>
#include <vector>

using byte = uint8_t;
using boolean = bool;
#define USE_RP2040 1
enum { MEM_KEYBD_CH = 8, MEM_KEYBD_COMPAT_MODE = 9, LOW, HIGH, OUTPUT, WD_PHASE_MODE_LOOP };
byte memory[65] = {};
int pinStatusLed, pinGBClock, blinkMaxCount, incomingMidiByte, countLSDJTicks;
bool usbMode = false;
void digitalWrite(int, int) {}
void pinMode(int, int) {}
void delayMicroseconds(int) {}
int statusFlashes = 0;
void statusLedOn() { ++statusFlashes; }
void updateStatusLed() {}
void ledRest(uint8_t) {}
void setMode() {}
void wdFeed(int) {}
bool checkForProgrammerSysex(byte) { return false; }
struct FakeSerial {
  bool available() { return false; }
  byte read() { return 0; }
  void write(byte) {}
} fakeSerial;
FakeSerial *serial = &fakeSerial;

//Decode the bytes actually clocked out by sendKeyboardByteToGameboy().
std::vector<byte> wire;
std::vector<int> frame;
void linkPins(int clock, int data, int) {
  if (!clock) return;
  frame.push_back(data);
  if (frame.size() != 11) return;
  assert(frame.front() == 0 && frame.back() == 1);
  byte value = 0;
  for (int i = 0; i < 8; ++i) value |= frame[i + 1] << i;
  wire.push_back(value);
  frame.clear();
}
#define GB_SET(c, d, i) linkPins(c, d, i)

struct UsbMidiMessage {
  uint8_t cin, status, data1, data2, length, source;
};
std::deque<UsbMidiMessage> usbQueue;
bool usbMidiReadMessage(UsbMidiMessage *message) {
  if (usbQueue.empty()) return false;
  *message = usbQueue.front();
  usbQueue.pop_front();
  return true;
}

//Use the sketch's own mapping and settings, not a second copy of the table.
#include "keyboard_settings.inc"
void sendKeyboardByteToGameboy(byte);
void changeLSDJInstrument(byte, byte);
void playLSDJNote(byte, byte, byte);
void changeLSDJOctave();
void modeLSDJKeyboard();
void modeLSDJKeyboardMidiReceive();
#include "../Arduinoboy/Mode_LSDJ_Keyboard.ino"

void reset() {
  statusFlashes = 0;
  memory[MEM_KEYBD_CH] = 15;
  memory[MEM_KEYBD_COMPAT_MODE] = 1;
  keyboardCurrentOct = keyboardLastOct = 0;
  keyboardCurrentIns = keyboardLastIns = 0;
  keyboardNoteStart = keyboardStartOctave + 12;
  keyboardCommands[7] = keyboardCurD;
  keyboardHeldMidiNote = 0xFF;
  keyboardMidiStatus = 0;
  keyboardMidiHasNote = false;
  wire.clear();
  frame.clear();
  usbQueue.clear();
}

void expect(std::initializer_list<byte> bytes) {
  if (wire != std::vector<byte>(bytes)) {
    std::cerr << "Unexpected PS/2 bytes:";
    for (byte b : wire) std::cerr << ' ' << std::hex << int(b);
    std::cerr << '\n';
    std::abort();
  }
  assert(frame.empty());
  wire.clear();
}

void din(std::initializer_list<byte> bytes) {
  for (byte b : bytes) handleLSDJKeyboardMidiByte(b);
}

void usb(byte source, byte status, byte note, byte velocity) {
  usbQueue.push_back({byte(status >> 4), status, note, velocity, 3, source});
  modeLSDJKeyboardMidiReceive();
}

int main() {
  reset();
  playLSDJNote(0x9F, 60, 64);
  playLSDJNote(0x9F, 60, 0);
  playLSDJNote(0x9F, 60, 64);
  playLSDJNote(0x9F, 60, 0);
  expect({0x06, 0x1A, 0xF0, 0x1A, 0x1A, 0xF0, 0x1A});

  reset();
  playLSDJNote(0x9F, 60, 64);
  playLSDJNote(0x9F, 60, 127); //duplicate Note On must still retrigger
  expect({0x06, 0x1A, 0xF0, 0x1A, 0x1A});
  playLSDJNote(0x9F, 60, 0);
  playLSDJNote(0x9F, 60, 0); //redundant Note Off
  expect({0xF0, 0x1A});

  reset();
  playLSDJNote(0x9F, 60, 64);
  playLSDJNote(0x9F, 72, 64); //same PS/2 key in a different octave
  expect({0x06, 0x1A, 0xF0, 0x1A, 0x06, 0x1A});
  playLSDJNote(0x9F, 60, 0); //release the older held piano key
  expect({});
  playLSDJNote(0x9F, 72, 0);
  expect({0xF0, 0x1A});
  assert(keyboardLastOct == 2);

  reset();
  playLSDJNote(0x9F, 60, 64);
  playLSDJNote(0x9F, 62, 64); //overlapping pitches in one octave
  expect({0x06, 0x1A, 0xF0, 0x1A, 0x22});
  playLSDJNote(0x9F, 60, 0);
  playLSDJNote(0x90, 62, 0); //wrong MIDI channel must not release it
  expect({});
  playLSDJNote(0x9F, 62, 0);
  expect({0xF0, 0x22});

  reset();
  playLSDJNote(0x90, 60, 64);
  playLSDJNote(0x9F, 35, 64);
  expect({});
  playLSDJNote(0x9F, 43, 64); //calibration/navigation keys stay press-only
  playLSDJNote(0x9F, 43, 0);
  playLSDJNote(0x9F, 43, 64);
  expect({0xE0, 0x72, 0xE0, 0x72});

  for (int note = 48; note <= 127; ++note) {
    reset();
    playLSDJNote(0x9F, note, 1);
    assert(!wire.empty());
    byte scan = wire.back();
    int octave = keyboardLastOct;
    wire.clear();
    playLSDJNote(0x9F, note, 0);
    expect({0xF0, scan});
    assert(keyboardLastOct == octave);
  }

  reset();
  //DIN/TRS activity must match USB: complete notes/releases and program
  //changes on the selected channel, including running-status messages.
  din({0x9F, 60});
  assert(statusFlashes == 0); //no flash for a partial message
  din({64, 60, 0, 60, 64, 0x8F, 60, 97});
  assert(statusFlashes == 4);
  expect({0x06, 0x1A, 0xF0, 0x1A, 0x1A, 0xF0, 0x1A});
  //Running-status Note Off, interleaved clocks, then Note On with note zero.
  din({60, 0xF8, 70, 0x9F, 0, 64, 60, 0xFE, 64, 60, 0});
  expect({0x1A, 0xF0, 0x1A});
  assert(statusFlashes == 8);
  //Neither SysEx nor other message types may be interpreted as notes.
  din({0x9F, 60, 0xF0, 60, 64, 0xF7, 60, 64, 0xBF, 60, 64});
  expect({});
  assert(statusFlashes == 8);
  din({0xCF, 1, 2}); //Program Change running status remains supported
  expect({0x0C, 0x0C});
  assert(statusFlashes == 10);
  din({0x90, 60, 64, 0x80, 60, 0});
  assert(statusFlashes == 10); //wrong channel stays dark
  expect({});

  for (byte source = 0; source != 2; ++source) {
    reset();
    usb(source, 0x9F, 60, 64);
    usb(source, 0x8F, 60, 64); //actual Note Off with release velocity
    usb(source, 0x9F, 60, 64);
    usb(source, 0x9F, 60, 0); //Note On velocity zero
    usb(source, 0x90, 60, 64); //wrong channel
    assert(statusFlashes == 4);
    expect({0x06, 0x1A, 0xF0, 0x1A, 0x1A, 0xF0, 0x1A});
  }

  std::cout << "PASS: repeated notes, overlapping notes, octaves, all 80 mapped MIDI notes, "
               "DIN running status, real-time/system bytes, USB host/device, channels, navigation and DIN/TRS activity LED\n";
}
