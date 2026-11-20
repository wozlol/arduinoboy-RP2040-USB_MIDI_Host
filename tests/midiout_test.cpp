//Build with c++ -std=c++11 tests/midiout_test.cpp -o /tmp/midiout_test
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#define USE_RP2040
using byte = uint8_t;
using boolean = bool;
enum { LOW, HIGH, INPUT_PULLUP, OUTPUT, pinGBClock, pinGBSerialIn, pinButtonMode, pinStatusLed, WD_PHASE_MODE_LOOP };
enum { MEM_FORCE_MODE, MEM_MIDIOUT_NOTE_CH, MEM_MIDIOUT_CC_CH = 5, MEM_MIDIOUT_CC_MODE = 9,
       MEM_MIDIOUT_CC_SCALING = 13, MEM_MIDIOUT_CC_NUMBERS = 17, MEM_LSDJSLAVE_MIDI_CH = 45 };
byte memory[65], incomingMidiByte, midiData[3], lastMidiData[2], midioutNoteHold[4][4], midioutNoteHoldCounter[4];
int countGbClockTicks, countClockPause, midioutBitDelay, blinkMaxCount;
int midiOutLastNote[4] = {-1, -1, -1, -1};
bool midiValueMode;
bool sequencerStarted, midiSyncEffectsTime, midiNoteOnMode, usbMode;
int countLSDJTicks, countSyncTime, countSyncPulse, countSyncSteps, midiDefaultStartOffset;
unsigned long midioutNoteTimer[4], midioutNoteTimerThreshold = 10;

unsigned long now, deadline;
std::vector<std::pair<unsigned long, unsigned long>> presses;
std::vector<byte> din, usb, fromGameBoy, afterResume;
std::vector<unsigned long> statusChanges;
int statusLevel = HIGH;
bool blinkSwitch[6];
byte linkIdleValue = 0xff;
int clockMode, fallingEdges, usbPolls, clockLevel = HIGH;
int pollStarts, edgesAtStart;
unsigned long firstEdge, lastEdge, ledRestAt[6];
struct Timeout {};
struct ModeChanged {};

unsigned long millis() { return now; }
void wdFeed(int) { if (++now >= deadline) throw Timeout{}; }
int clockLatch;
void updateClockLevel() {
  int next = clockMode == OUTPUT ? clockLatch : HIGH;
  if (clockLevel == HIGH && next == LOW) {
    if (fallingEdges++ == 0) firstEdge = now;
    lastEdge = now;
  }
  clockLevel = next;
}
void gpio_put(int pin, int value) { assert(pin == pinGBClock); clockLatch = value; }
void pinMode(int pin, int mode) {
  assert(pin == pinGBClock);
  if (mode == OUTPUT) {
    ++pollStarts;
    edgesAtStart = fallingEdges;
  }
  if (mode == INPUT_PULLUP) clockLatch = LOW;
  clockMode = mode;
  updateClockLevel();
}
void digitalWrite(int pin, int value) {
  if (pin == pinStatusLed) {
    if (statusLevel != value) statusChanges.push_back(now);
    statusLevel = value;
    return;
  }
  assert(pin == pinGBClock && value == HIGH);
  if (clockMode == OUTPUT) clockLatch = value;
  updateClockLevel();
}
void linkPins(int clock, int, int) {
  assert(clockMode == OUTPUT);
  clockLatch = clock;
  updateClockLevel();
}
#define GB_SET(c,d,i) linkPins(c,d,i)
int digitalRead(int pin) {
  if (pin == pinButtonMode) {
    for (auto press : presses) if (press.first <= now && now < press.second) return LOW;
    return HIGH;
  }
  assert(pin == pinGBSerialIn && fallingEdges > 0);
  unsigned bit = fallingEdges - edgesAtStart - 1;
  const auto &bytes = pollStarts == 1 ? fromGameBoy : afterResume;
  byte value = bit / 8 < bytes.size() ? bytes[bit / 8] : linkIdleValue;
  return (value >> (7 - bit % 8)) & 1;
}
void setMode(byte mode) { assert(mode == 0); throw ModeChanged{}; }
void setMode() { if (!memory[MEM_FORCE_MODE] && digitalRead(pinButtonMode) == LOW) setMode(0); }
void ledRest(byte led) {
  ledRestAt[led] = now;
  if (led == 5) digitalWrite(pinStatusLed, HIGH);
}
void updateBlinkLights() {}
void updateStatusLight() {}
void updateVisualSync() {}
void sequencerStart() { sequencerStarted = true; }
void sequencerStop() { sequencerStarted = false; }
void blinkLight(int, int) {}
void delayMicroseconds(int) {}
struct UsbMidiMessage { byte status, data1, data2; };
std::vector<std::pair<unsigned long, byte>> usbEvents;
size_t nextEvent;
bool usbMidiReadMessage(UsbMidiMessage *rx) {
  ++usbPolls;
  if (nextEvent == usbEvents.size() || usbEvents[nextEvent].first > now) return false;
  *rx = {usbEvents[nextEvent++].second, 0, 0};
  return true;
}
void usbMidiSendRTMessage(byte value) { usb.push_back(value); }
void usbMidiSendThreeByteMessage(byte a, byte b, byte c) { usb.insert(usb.end(), {a,b,c}); }
void usbMidiSendTwoByteMessage(byte a, byte b) { usb.insert(usb.end(), {a,b}); }
bool checkForProgrammerSysex(byte) { return false; }
struct MidiPort {
  bool available() { return false; }
  byte read() { return 0; }
  void write(byte value) { din.push_back(value); }
  void write(byte *data, int n) { din.insert(din.end(), data, data+n); }
} midi;
MidiPort *serial = &midi;

bool getIncommingSlaveByte();
void midioutDoAction(byte, byte);
void checkStopNote(byte);
void stopNote(byte);
void playNote(byte, byte);
void playCC(byte, byte);
void playPC(byte, byte);
void stopAllNotes();
#include "../Arduinoboy/Mode_LSDJ_Midiout.ino"
void modeLSDJSlaveSync();
void modeLSDJSlaveSyncUsbMidiReceive();
void sendClockTickToLSDJ();
void getSlaveSyncEffect(byte);
#include "../Arduinoboy/Mode_LSDJ_SlaveSync.ino"

bool run(unsigned long until) {
  now = firstEdge = lastEdge = 0;
  fallingEdges = usbPolls = pollStarts = 0;
  deadline = until;
  din.clear();
  usb.clear();
  statusLevel = HIGH;
  statusChanges.clear();
  for (int channel = 0; channel < 4; ++channel) {
    midiOutLastNote[channel] = -1;
    midioutNoteHoldCounter[channel] = 0;
    midioutNoteTimer[channel] = 0;
  }
  try { modeLSDJMidioutSetup(); }
  catch (ModeChanged &) { return true; }
  catch (Timeout &) { return false; }
  assert(false);
  return false;
}

void checkSlowBlink(unsigned long pressedAt) {
  assert(statusChanges.size() == 4 && statusLevel == HIGH);
  assert(statusChanges.front() == pressedAt + 1000 && firstEdge == statusChanges.front() + 1);
  for (size_t i = 1; i < statusChanges.size(); ++i)
    assert(statusChanges[i] - statusChanges[i-1] == 1000);
}

int main() {
  //Saved or remotely selected mode 7 stays quiet indefinitely.
  assert(!run(10000));
  assert(clockMode == INPUT_PULLUP && fallingEdges == 0 && usbPolls > 9000);
  assert(statusChanges.empty() && statusLevel == HIGH);

  //Holding the button from the previous mode cannot start clocks.
  //The next short press wraps to mode 1, even if the button bounces.
  presses = {{0,2000}, {2200,2204}, {2210,2350}};
  assert(run(3000));
  assert(clockMode == INPUT_PULLUP && fallingEdges == 0);
  assert(statusChanges.empty());

  //Even with release debounce, 999 ms stays a short press.
  presses = {{100,1099}};
  assert(run(1200));
  assert(statusChanges.empty() && fallingEdges == 0);

  //In forced mode a short press stays parked. A fresh long press starts MIDIOUT.
  memory[MEM_FORCE_MODE] = 1;
  presses = {{100,200}, {400,1400}}; //Release exactly at the threshold.
  fromGameBoy = {0xfd,0xf0,0xb0}; //Start, select PU1, note 48
  linkIdleValue = 0; //After the note, the Game Boy has no more data.
  blinkSwitch[5] = true; //A pending activity flash from the previous mode.
  assert(!run(4700));
  assert(din.size() >= 4 && usb == din);
  assert((std::vector<byte>(din.begin(),din.begin()+4) == std::vector<byte>{0xfa,0x90,48,127}));
  checkSlowBlink(400);
  assert(!blinkSwitch[5]);

  //Keep blinking and polling during the original hold. Release bounce must
  //not leave mode 7. A tap exits on release even on a continuously busy link.
  memory[MEM_FORCE_MODE] = 0;
  fromGameBoy.clear();
  linkIdleValue = 0xff; //A continuous stream of clock messages.
  presses = {{100,3700}, {3708,3712}, {4350,4450}};
  assert(run(4500));
  assert(now == 4470 && fallingEdges > 0 && clockMode == INPUT_PULLUP);
  checkSlowBlink(100);

  //A second hold releases the clock and a sounding note exactly at one second.
  //Keeping it down for several seconds must leave polling off and all LEDs on.
  fromGameBoy = {0xf0, 0xbc, 0xf2}; //PU1 note 60, then a command missing its value.
  linkIdleValue = 0;
  presses = {{100,1450}, {2300,6000}, {6500,7600}, {7800,7900}};
  assert(!run(6200));
  assert(pollStarts == 1 && firstEdge == 1101 && lastEdge == 3299 && clockMode == INPUT_PULLUP);
  assert((statusChanges == std::vector<unsigned long>{1100, 2100, 3100, 3300}));
  assert(statusLevel == HIGH);
  for (auto restored : ledRestAt) assert(restored == 3300);
  assert((din == std::vector<byte>{0x90,60,127, 0x80,60,0}) && usb == din);
  assert(midiOutLastNote[0] == -1 && midioutNoteHoldCounter[0] == 0);

  //Resume between a channel command and its value. Releasing PU1 during the
  //pause must preserve the pending WAV channel for the next note.
  memory[MEM_MIDIOUT_NOTE_CH + 2] = 2;
  afterResume = {0xc3}; //Value 67 for the pending WAV command.
  memory[MEM_FORCE_MODE] = 1;
  assert(!run(8100));
  assert(pollStarts == 2 && clockMode == OUTPUT && lastEdge == 8099);
  assert(statusChanges.back() == 7500 && statusLevel == LOW);
  assert((din == std::vector<byte>{0x90,60,127, 0x80,60,0, 0x92,67,127}) && usb == din);
  //A short tap in active mode releases that note and exits when unlocked.
  memory[MEM_FORCE_MODE] = 0;
  assert(run(8100));
  assert(now == 7920 && lastEdge == 7919 && clockMode == INPUT_PULLUP);
  assert((din == std::vector<byte>{0x90,60,127, 0x80,60,0, 0x92,67,127, 0x82,67,0}) && usb == din);

  //Clock received in mode 7 is discarded. Mode 1 waits for a fresh start;
  //each playing MIDI tick then produces exactly eight falling edges.
  presses = {{500,600}};
  usbEvents = {{100,0xfa}, {101,0xf8}, {800,0xf8}, {900,0xfa},
               {910,0xf8}, {920,0xf8}, {930,0xfc}, {940,0xf8}};
  assert(run(1000));
  assert(fallingEdges == 0 && nextEvent == 2);
  sequencerStarted = true;
  try { modeLSDJSlaveSyncSetup(); } catch (Timeout &) {}
  assert(firstEdge == 910 && fallingEdges == 16 && clockMode == INPUT_PULLUP);
  assert(nextEvent == usbEvents.size() && !sequencerStarted);
  std::puts("PASS: mode 7 polling toggle, held-note release, slow status blink, forced mode, busy exit and mode 1 clock");
}
