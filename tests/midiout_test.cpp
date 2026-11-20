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
byte memory[65], incomingMidiByte, midiData[3], lastMidiData[2], midioutNoteHold[4][4];
int countGbClockTicks, countClockPause, midioutBitDelay, blinkMaxCount, midioutNoteHoldCounter[4];
int midiOutLastNote[4] = {-1, -1, -1, -1};
bool midiValueMode;
bool sequencerStarted, midiSyncEffectsTime, midiNoteOnMode, usbMode;
int countLSDJTicks, countSyncTime, countSyncPulse, countSyncSteps, midiDefaultStartOffset;
unsigned long midioutNoteTimer[4], midioutNoteTimerThreshold = 10;

unsigned long now, deadline;
std::vector<std::pair<unsigned long, unsigned long>> presses;
std::vector<byte> din, usb, fromGameBoy;
std::vector<unsigned long> statusChanges;
int statusLevel = HIGH;
bool blinkSwitch[6];
byte linkIdleValue = 0xff;
int clockMode, fallingEdges, usbPolls, clockLevel = HIGH;
unsigned long firstEdge;
struct Timeout {};
struct ModeChanged {};

unsigned long millis() { return now; }
void wdFeed(int) { if (++now >= deadline) throw Timeout{}; }
int clockLatch;
void updateClockLevel() {
  int next = clockMode == OUTPUT ? clockLatch : HIGH;
  if (clockLevel == HIGH && next == LOW && fallingEdges++ == 0) firstEdge = now;
  clockLevel = next;
}
void gpio_put(int pin, int value) { assert(pin == pinGBClock); clockLatch = value; }
void pinMode(int pin, int mode) {
  assert(pin == pinGBClock);
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
  unsigned bit = fallingEdges - 1;
  byte value = bit / 8 < fromGameBoy.size() ? fromGameBoy[bit / 8] : linkIdleValue;
  return (value >> (7 - bit % 8)) & 1;
}
void setMode(byte mode) { assert(mode == 0); throw ModeChanged{}; }
void setMode() { if (!memory[MEM_FORCE_MODE] && digitalRead(pinButtonMode) == LOW) setMode(0); }
void ledRest(byte) {}
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

void modeLSDJMidiout();
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
  now = firstEdge = 0;
  fallingEdges = usbPolls = 0;
  deadline = until;
  din.clear();
  usb.clear();
  statusLevel = HIGH;
  statusChanges.clear();
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
  //not leave mode 7. A fresh press exits even on a continuously busy link.
  memory[MEM_FORCE_MODE] = 0;
  fromGameBoy.clear();
  linkIdleValue = 0xff; //A continuous stream of clock messages.
  presses = {{100,3700}, {3708,3712}, {4350,6000}};
  assert(run(4500));
  assert(now == 4370 && fallingEdges > 0);
  checkSlowBlink(100);

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
  std::puts("PASS: silent mode 7, forced mode, MIDIOUT messages, slow status blink, busy exit and mode 1 clock");
}
