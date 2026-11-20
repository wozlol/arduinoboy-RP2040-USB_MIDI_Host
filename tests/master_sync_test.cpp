//Build with c++ -std=c++11 tests/master_sync_test.cpp -o /tmp/master_sync_test
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using byte = uint8_t;
using boolean = bool;
enum { INPUT, INPUT_PULLUP, pinGBClock, pinGBSerialIn, WD_PHASE_MODE_LOOP };
#define USE_RP2040
constexpr int MEM_LSDJMASTER_MIDI_CH = 0;
byte memory[] = {15};
byte incomingMidiByte, readgbClockLine, readGbSerialIn, bit;
int countGbClockTicks, countClockPause, countSyncTime, blinkMaxCount;
bool sequencerStarted, usbMode;

std::vector<byte> din, usb;
struct MidiPort {
  bool available() { return false; }
  byte read() { return 0; }
  void write(byte value) { din.push_back(value); }
} midi;
MidiPort *serial = &midi;
void usbMidiSendThreeByteMessage(byte a, byte b, byte c) { usb.insert(usb.end(), {a, b, c}); }
void usbMidiSendRTMessage(byte value) { usb.push_back(value); }
bool checkForProgrammerSysex(byte) { return false; }
void wdFeed(int) {}
void setMode() {}
void ledRest(int) {}
void updateStatusLight() {}
void sequencerStart() { sequencerStarted = true; }
void sequencerStop() { sequencerStarted = false; }
int beats, idleUsbPolls;
size_t cursor;
void updateVisualSync() { ++beats; }
void usbMidiUpdate() { if (cursor < 50000) ++idleUsbPolls; }

struct Sample { int clock; byte data; };
std::vector<Sample> samples;
byte dataPin;
int clockPinMode;
struct EndOfTrace {};
void pinMode(int pin, int mode) { assert(pin == pinGBClock); clockPinMode = mode; }
int digitalRead(int pin) {
  if (pin == pinGBSerialIn) return dataPin;
  assert(pin == pinGBClock);
  if (cursor == samples.size()) throw EndOfTrace{};
  const auto sample = samples[cursor++];
  dataPin = sample.data;
  return sample.clock < 0 ? (clockPinMode == INPUT_PULLUP) : sample.clock;
}

void modeLSDJMasterSync();
bool checkLSDJStopped();
void sendMidiClockSlaveFromLSDJ();
#include "../Arduinoboy/Mode_LSDJ_MasterSync.ino"

void appendBits(byte value, int bits = 8) {
  for (int i = 7; i >= 8 - bits; --i) {
    byte data = (value >> i) & 1;
    samples.push_back({1, data});
    samples.push_back({0, data});
  }
}

int main() {
  //Float the clock, introduce a partial transfer, then leave the cable out.
  samples.insert(samples.end(), 50000, {-1, 0});
  appendBits(0xff, 3);
  samples.insert(samples.end(), 50000, {-1, 0});
  //Two ticks, unplug during the next byte, then restart on a different song row.
  appendBits(0x35);
  appendBits(0);
  appendBits(0xff, 7);
  samples.insert(samples.end(), 50000, {-1, 0});
  appendBits(0x12);
  samples.insert(samples.end(), 50000, {-1, 0});

  try { modeLSDJMasterSyncSetup(); } catch (EndOfTrace &) {}
  const std::vector<byte> expected = {
    0x9f, 0x35, 127, 0xfa, 0xf8, 0xf8, 0xfc,
    0x9f, 0x12, 127, 0xfa, 0xf8, 0xfc
  };
  assert(clockPinMode == INPUT_PULLUP);
  assert(din == expected && usb == expected);
  assert(beats == 3 && !sequencerStarted);
  assert(idleUsbPolls > 1);
  std::puts("PASS: idle, partial transfers, clock/row output, stop and reconnect");
}
