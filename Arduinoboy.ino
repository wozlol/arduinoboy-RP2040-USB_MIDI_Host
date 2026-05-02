/***************************************************************************
 ***************************************************************************
 *                         __      _             __                        *
 *         ____ __________/ /_  __(_)___  ____  / /_  ____  __  __         *
 *        / __ `/ ___/ __  / / / / / __ \/ __ \/ __ \/ __ \/ / / /         *
 *       / /_/ / /  / /_/ / /_/ / / / / / /_/ / /_/ / /_/ / /_/ /          *
 *       \__,_/_/   \__,_/\__,_/_/_/ /_/\____/_.___/\____/\__, /           *
 *                                                       /____/            *
 *                                                                         *
 ***************************************************************************
 ***************************************************************************
 *                                                                         *
 * Version: 1.3.5 - RP2040 USB MIDI device and host update                 *
 * Date:    April 29 2026                                                  *
 * Name:    Joseph Wozniak                                                 *
 * Email:   woz@woz.lol                                                    *
 *                                                                         *
 * Adds RP2040 / Arduino-Pico support, built-in USB MIDI device support,   *
 * and PIO-USB host support for class-compliant USB MIDI keyboards.        *
 * (tested only on the RP2040 Zero module)                                 *
 *                                                                         *
 * Previous version: 1.3.4 - Modified by Entropy Electronics for ProMicro  *
 * Date:    May 19 2020                                                    *
 * Original author: Timothy Lamb                                           *
 * Email:   trash80@gmail.com                                              *
 *                                                                         *
 ***************************************************************************
 ***************************************************************************
 *                                                                         *
 * NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   *
 *                                                                         *
 *  https://github.com/trash80/Arduinoboy                                  *
 *                                                                         *
 *   Arduino pin settings:  (Layout is final)                              *
 *     - 6 LEDS on pins 8 to 13                                            *
 *     - Push button on pin 3 (for selecting mode)                         *
 *     - MIDI Opto-isolator power on pin 4                                 *
 *     - Gameboy Clock line on analog in pin 0                             *
 *     - Gameboy Serial Data input on analog in pin 1                      *
 *     - Serial Data from gameboy on analog in pin 2                       *
 *                                                                         *
 *   Teensy pin settings:                                                  *
 *     - 6 LEDS on pins 23,22,21,20,4,13                                   *
 *     - Push button on pin 2 (for selecting mode)                         *
 *     - MIDI Opto-isolator power connected to +3v                         *
 *     - Gameboy Clock line on pin 16                                      *
 *     - Gameboy Serial Data input on analog in pin 17                     *
 *     - Serial Data from gameboy on analog in pin 18                      *
 *                                                                         *
 *   Teensy USB MIDI is supported                                          *
 *   Teensy LC should work but untested                                    *
 *                                                                         *
 *   RP2040 / Arduino-Pico pin settings:                                   *
 *     - 6 LEDS on GPIO pins 9,10,11,12,13,16                              *
 *     - Push button on GPIO pin 5 (for selecting mode)                    *
 *     - MIDI Opto-isolator power not used                                 *
 *     - Gameboy Clock line on GPIO pin 6                                  *
 *     - Gameboy Serial Data input on GPIO pin 7                           *
 *     - Serial Data from gameboy on GPIO pin 8                            *
 *     - USB MIDI host D+ to 22ohm resistor to GPIO pin 14                 *
 *     - USB MIDI host D- to 22ohm resistor to GPIO pin 15                 * 
 *     - USB MIDI host port requires 5V VBUS power, and two 15k resistors  *
 *     one from D+ to ground, and another from D- to ground.               * 
 *                                                                         *
 *     -IMPORTANT set CPU Speed: "240 MHz (Overclock)"                     *
 *     -NOTE use a ground wire to the usb host thicker than 30awg.         *
 *     -Twist D+ and D-, try to keep them short.                           *
 *     -The 22ohm resistors go near the RP2040.                            *
 *                                                                         *
 *   RP2040 built-in USB works as a USB MIDI device.                       *
 *   RP2040 GPIO14/GPIO15 port works as a USB MIDI host.                   *
 *                                                                         *
 * Program Information:                                                    *
 *    LSDJ Slave Mode Midi Note Effects:                                   *
 *      48 - C-2 Sends a Sequencer Start Command                           *
 *      49 - C#2 Sends a Sequencer Stop Command                            *
 *      50 - D-2 Toggles Normal Tempo                                      *
 *      51 - D#2 Toggles 1/2 Tempo                                         *
 *      52 - E-2 Toggles 1/4 Tempo                                         *
 *      53 - F-2 Toggles 1/8 Tempo                                         *
 *                                                                         *
 *    LSDJ Keyboard Mode:                                                  *
 *      48 - C-2 Mute Pu1 Off/On                                           *
 *      49 - C#2 Mute Pu2 Off/On                                           *
 *      50 - D-2 Mute Wav Off/On                                           *
 *      51 - D#2 Mute Noi Off/On                                           *
 *      52 - E-2 Livemode Cue Sequence                                     *
 *      53 - F-2 Livemode Cursor Up                                        *
 *      54 - F#2 Livemode Cursor Down                                      *
 *      55 - G-2 Livemode Cursor Left                                      *
 *      56 - G#2 Livemode Cursor Right                                     *
 *      57 - A-2 Table Up                                                  *
 *      58 - A#2 Table Down                                                *
 *      59 - B-2 Cue Table                                                 *
 *      60 - C-3 to C-8 Notes!                                             *
 *      Prgram Change to select from instrument table                      *
 *                                                                         *
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#define MEM_MAX 65
#define NUMBER_OF_MODES 7    //Right now there are 7 modes, Might be more in the future

//!!! do not edit these, they are the position in EEPROM memory that contain the value of each stored setting
#define MEM_CHECK 0
#define MEM_VERSION_FIRST 1
#define MEM_VERSION_SECOND 2
#define MEM_MODE 5
#define MEM_FORCE_MODE 4

#define MEM_LSDJSLAVE_MIDI_CH 6

#define MEM_LSDJMASTER_MIDI_CH 7
#define MEM_KEYBD_CH 8

#define MEM_KEYBD_COMPAT_MODE 9
#define MEM_KEYBD_CH_TO_INST 10

#define MEM_MIDIOUT_NOTE_CH 11
#define MEM_MIDIOUT_CC_CH 15
#define MEM_MIDIOUT_CC_MODE 19
#define MEM_MIDIOUT_CC_SCALING 23
#define MEM_MIDIOUT_CC_NUMBERS 27

#define MEM_MGB_CH 55
#define MEM_LIVEMAP_CH 60

#define MEM_MIDIOUT_BIT_DELAY 61
#define MEM_MIDIOUT_BYTE_DELAY 63

/***************************************************************************
* User Settings
***************************************************************************/

boolean usbMode                  = false; //to use usb for serial communication as oppose to MIDI - sets baud rate to 38400

byte defaultMemoryMap[MEM_MAX] = {
  0x7F,0x01,0x03,0x7F, // memory init check

  0x01, // force mode ON, ignore EEPROM mode changes, boots fixed
  0x04, // boot mode = internal mode 4 = README "Mode 5 Full MIDI with mGB"

  15,   // LSDJ slave sync MIDI channel, unused here
  15,   // LSDJ master note position MIDI channel, unused here
  15,   // keyboard MIDI channel, unused here

  1,    // keyboard compatibility mode
  1,    // keyboard ch to instrument

  0,1,2,3, // LSDJ midiout note channels
  0,1,2,3, // LSDJ midiout CC channels

  1,1,1,1, // CC mode
  1,1,1,1, // CC scaling

  1,2,3,7,10,11,12, // pu1 CC numbers
  1,2,3,7,10,11,12, // pu2
  1,2,3,7,10,11,12, // wav
  1,2,3,7,10,11,12, // noi

  1, 2, 3, 9, 0, // mGB MIDI channels:
                  // mGB ch1 <- MIDI ch2
                  // mGB ch2 <- MIDI ch3
                  // mGB ch3 <- MIDI ch4
                  // mGB ch4 drums/noise <- MIDI ch10
                  // mGB ch5/global <- MIDI ch1

  0,    // livemap channel start
  80,1, // midiout bit delay
  0,0   // midiout byte delay
};
byte memory[MEM_MAX];

/***************************************************************************
* Lets Assign our Arduino Pins .....
***************************************************************************/


/***************************************************************************
* Teensy 3.2, Teensy LC
*
* Notes on Teensy: Pins are not the same as in the schematic, the mapping is below.
* Feel free to change, all related config in is this block.
* Be sure to compile
***************************************************************************/
#if defined (__MK20DX256__) || defined (__MK20DX128__) || defined (__MKL26Z64__)
#define USE_TEENSY 1
#define USE_USB 1
#include <MIDI.h>

#if defined (__MKL26Z64__)
#define GB_SET(bit_cl,bit_out,bit_in) GPIOB_PDOR = ((bit_in<<3) | (bit_out<<1) | bit_cl)
#else
#define GB_SET(bit_cl,bit_out,bit_in) GPIOB_PDOR = (GPIOB_PDIR & 0xfffffff4) | ((bit_in<<3) | (bit_out<<1) | bit_cl)
#endif

int pinGBClock     = 16;    // Analog In 0 - clock out to gameboy
int pinGBSerialOut = 17;    // Analog In 1 - serial data to gameboy
int pinGBSerialIn  = 18;    // Analog In 2 - serial data from gameboy
int pinMidiInputPower = 0; // Not used!
int pinStatusLed = 13; // Status LED
int pinLeds[] = {23,22,21,20,4,13}; // LED Pins
int pinButtonMode = 2; //toggle button for selecting the mode

HardwareSerial *serial = &Serial1;

/***************************************************************************
* Arduino Leonardo/Yún/Micro (ATmega32U4)
***************************************************************************/
#elif defined (__AVR_ATmega32U4__)
#define USE_LEONARDO
#include <MIDIUSB.h>

#define GB_SET(bit_cl, bit_out, bit_in) PORTF = (PINF & B00011111) | ((bit_cl<<7) | ((bit_out)<<6) | ((bit_in)<<5))
// ^ The reason for not using digitalWrite is to allign clock and data pins for the GB shift reg.
// Pin distribution comes from official Arduino Leonardo documentation

int pinGBClock     = A0;    // Analog In 0 - clock out to gameboy
int pinGBSerialOut = A1;    // Analog In 1 - serial data to gameboy
int pinGBSerialIn  = A2;    // Analog In 2 - serial data from gameboy
int pinMidiInputPower = 4; // power pin for midi input opto-isolator
int pinStatusLed = 9; // Status LED
int pinLeds[] = {3, 5, 6, 7, 8, 9}; // LED Pins
//int pinStatusLed = 13; // Status LED
//int pinLeds[] = {12,11,10,9,8,13}; // LED Pins
int pinButtonMode = 10; //toggle button for selecting the mode

HardwareSerial *serial = &Serial1;



/***************************************************************************
* Arduino Due (ATmSAM3X8E)
***************************************************************************/
#elif defined (__SAM3X8E__)
#define USE_DUE

#define USE_LEONARDO
#include <MIDIUSB.h>
#include <digitalWriteFast.h>


#define GB_SET(bit_cl, bit_out, bit_in) digitalWriteFast(A0, bit_cl); digitalWriteFast(A1, bit_out); digitalWriteFast(A2, bit_in);
// ^ The reason for not using digitalWrite is to allign clock and data pins for the GB shift reg.

int pinGBClock     = A0;    // Analog In 0 - clock out to gameboy
int pinGBSerialOut = A1;    // Analog In 1 - serial data to gameboy
int pinGBSerialIn  = A2;    // Analog In 2 - serial data from gameboy
int pinMidiInputPower = 4; // power pin for midi input opto-isolator
int pinStatusLed = 13; // Status LED
int pinLeds[] = {12,11,10,9,8,13}; // LED Pins
int pinButtonMode = 3; //toggle button for selecting the mode

HardwareSerial *serial = &Serial;

/***************************************************************************
* RP2040 / Arduino-Pico
*
* Built-in USB is a USB MIDI device. A second USB port works as a USB MIDI
* host for class-compliant USB MIDI keyboards using PIO-USB.
***************************************************************************/
#elif defined (ARDUINO_ARCH_RP2040)
#define USE_RP2040 1
#define USE_USB 1
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <hardware/clocks.h>
#include "pio_usb_configuration.h"
#include "pio_usb.h"

#if defined(F_CPU) && (F_CPU != 120000000UL) && (F_CPU != 240000000UL)
#error "RP2040 PIO USB host requires Arduino-Pico CPU Speed set to 120 MHz or 240 MHz."
#endif

#define GB_SET(bit_cl, bit_out, bit_in) digitalWrite(pinGBClock, bit_cl); digitalWrite(pinGBSerialOut, bit_out); digitalWrite(pinGBSerialIn, bit_in)
#define USB_MIDI_MESSAGE_DEFINED 1
struct UsbMidiMessage {
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
  uint8_t length;
};
bool usbMidiReadMessage(UsbMidiMessage *msg);
void usbMidiInit();
void usbMidiStartHost();

int pinGBClock     = 6; // Game Boy clock line
int pinGBSerialOut = 7; // Serial data to Game Boy
int pinGBSerialIn  = 8; // Serial data from Game Boy
int pinMidiInputPower = 0; // Not used on RP2040 USB/DIN builds
int pinStatusLed = 13;
int pinLeds[] = {9, 10, 11, 12, 13, 16};
int pinButtonMode = 5;

HardwareSerial *serial = &Serial1;

/***************************************************************************
* Arduino UNO/Ethernet/Nano (ATmega328), Arduino UNO Wifi (ATmega4809) or Mega 2560 (ATmega2560/ATmega1280) (assumed)
***************************************************************************/
#else

#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
#define GB_SET(bit_cl,bit_out,bit_in) PORTF = (PINF & B11111000) | ((bit_in<<2) | ((bit_out)<<1) | bit_cl)
#elif defined(__AVR_ATmega4809__)
#define GB_SET(bit_cl,bit_out,bit_in) PORTD = (PIND & B11111000) | ((bit_in<<2) | ((bit_out)<<1) | bit_cl)
#else
#define GB_SET(bit_cl,bit_out,bit_in) PORTC = (PINC & B11111000) | ((bit_in<<2) | ((bit_out)<<1) | bit_cl)
#endif
// ^ The reason for not using digitalWrite is to allign clock and data pins for the GB shift reg.

int pinGBClock     = A0;    // Analog In 0 - clock out to gameboy
int pinGBSerialOut = A1;    // Analog In 1 - serial data to gameboy
int pinGBSerialIn  = A2;    // Analog In 2 - serial data from gameboy
int pinMidiInputPower = 4; // power pin for midi input opto-isolator
int pinStatusLed = 13; // Status LED
int pinLeds[] = {12,11,10,9,8,13}; // LED Pins
int pinButtonMode = 3; //toggle button for selecting the mode

HardwareSerial *serial = &Serial;

#endif

/***************************************************************************
* Memory
***************************************************************************/
#ifndef USE_DUE
    #include <EEPROM.h>
    boolean alwaysUseDefaultSettings = false; //set to true to always use the settings below, else they are pulled from memory for the software editor
#else
    boolean alwaysUseDefaultSettings = true; //set to true to always use the default settings, in Due board is necessary as it doesn't have EEPROM
#endif

int eepromMemoryByte = 0; //Location of where to store settings from mem

/***************************************************************************
* Sysex Settings & vars
***************************************************************************/

boolean sysexReceiveMode = 0;
boolean sysexProgrammingMode = 0;
boolean sysexProgrammingWaiting = 0;
boolean sysexProgrammingConnected = 0;

unsigned long sysexProgrammerWaitTime = 2000; //2 seconds
unsigned long sysexProgrammerCallTime = 1000; //1 second
unsigned long sysexProgrammerLastResponse = 0;
unsigned long sysexProgrammerLastSent = 0;

byte sysexManufacturerId = 0x69; //har har harrrrr :)
int sysexPosition;
byte sysexData[128];
byte longestSysexMessage = 128;

int midioutBitDelay = 0;
int midioutByteDelay = 0;

/***************************************************************************
* Switches and states
***************************************************************************/
boolean sequencerStarted = false;        //Sequencer has Started
boolean midiSyncEffectsTime = false;
boolean midiNoteOnMode   =false;
boolean midiNoteOffMode  =false;
boolean midiProgramChange=false;
boolean midiAddressMode  =false;
boolean midiValueMode    =false;

int midiOutLastNote[4] = {-1,-1,-1,-1};

boolean statusLedIsOn    =false;
boolean statusLedBlink   =false;

boolean nanoState        =false;
boolean nanoSkipSync     =false;

int buttonDepressed;
int buttonState;
unsigned long int buttonProgrammerWaitTime = 2000; //2 whole seconds
unsigned long int buttonTime;


boolean blinkSwitch[6];
unsigned long int blinkSwitchTime[6];
uint8_t switchLight = 0;

uint16_t blinkMaxCount = 1000;

unsigned long midioutNoteTimer[4];
byte midioutNoteHold[4][4];
byte midioutNoteHoldCounter[4];
int midioutNoteTimerThreshold = 10;

/***************************************************************************
* Counter vars
***************************************************************************/
int countLSDJTicks = 0;            //for loop int (we need to cycle 8 pulses)
int countSyncTime  = 0;
int countSyncLightTime=0;
int countSyncSteps = 0;
int countSyncPulse = 0;
int countGbClockTicks =0;
int countClockPause =0;
int countIncommingMidiByte =0;
int countStatusLedOn =0;
unsigned int waitClock =0;


int miscLastLed;
unsigned long int miscLedTime;
unsigned long int miscLedMaxTime;

/***************************************************************************
* Inbound Data Placeholders
***************************************************************************/
byte incomingMidiByte;  //incomming midi message
byte readgbClockLine;
byte readGbSerialIn;
byte bit;
byte midiData[] = {0, 0, 0};
byte lastMidiData[] = {0, 0, 0};

int incomingMidiNote = 0;
int incomingMidiVel = 0;
byte readToggleMode;
byte serialWriteBuffer[256];
byte midiDefaultStartOffset;
int  writePosition=0;
int  readPosition=0;
int lastMode=0; //Stores the last selected mode for leds.

byte midiSyncByte;
byte midiSyncByteLast;

byte midiStatusType;
byte midiStatusChannel;

/***************************************************************************
* LSDJ Keyboard mode settings
***************************************************************************/
byte keyboardNotes[] = {0x1A,0x1B,0x22,0x23,0x21,0x2A,0x34,0x32,0x33,0x31,0x3B,0x3A,
                         0x15,0x1E,0x1D,0x26,0x24,0x2D,0x2E,0x2C,0x36,0x35,0x3D,0x3C};
byte keyboardOctDn = 0x05;
byte keyboardOctUp = 0x06;

byte keyboardInsDn = 0x04;
byte keyboardInsUp = 0x0C;

byte keyboardTblDn = 0x03;
byte keyboardTblUp = 0x0B;

byte keyboardTblCue= 0x29;

byte keyboardMut1 = 0x01;
byte keyboardMut2 = 0x09;
byte keyboardMut3 = 0x78;
byte keyboardMut4 = 0x07;

byte keyboardCurL = 0x6B;
byte keyboardCurR = 0x74;
byte keyboardCurU = 0x75;
byte keyboardCurD = 0x72;
byte keyboardPgUp = 0x7D;
byte keyboardPgDn = 0x7A;
byte keyboardEntr = 0x5A;

int keyboardCurrentOct = 0;
int keyboardCurrentIns = 0;
int keyboardCurrentTbl = 0;

int keyboardLastOct = 0;
int keyboardLastIns = 0;
int keyboardLastTbl = 0;

int keyboardDiff = 0;
int keyboardCount = 0;
byte keyboardStartOctave = 0x24;
byte keyboardNoteStart = 0;
byte keyboardNoteOffset = 0;
byte keyboardCommands[12];

/***************************************************************************
* LSDJ Midi Map mode vars
***************************************************************************/
int  mapCurrentRow = -1;
int  mapQueueMessage = -1;
unsigned long mapQueueTime;
// mapQueueWait is used for delaying a sync byte
// if it is called right before a note on message on sequencer start
// (Note value is also a clock tick)
uint8_t mapQueueWaitSerial = 2; //2ms
uint8_t mapQueueWaitUsb = 5; //5ms - Needs to be longer because message packet is processed all at once

/***************************************************************************
* mGB Settings
***************************************************************************/
#define GB_MIDI_DELAY 500 //Microseconds to delay the sending of a byte to gb

void setup() {
/*
  Init Memory
*/
#ifdef USE_RP2040
  usbMidiInit();
  Serial.begin(115200); // Required by Arduino-Pico when using the TinyUSB stack.
  EEPROM.begin(256);
#endif
  initMemory(0);
/*
  Init Pins
*/
  for(int led=0;led<=5;led++) pinMode(pinLeds[led],OUTPUT);
  pinMode(pinStatusLed,OUTPUT);
  pinMode(pinButtonMode,INPUT_PULLUP);

  pinMode(pinGBClock,OUTPUT);
  pinMode(pinGBSerialIn,INPUT);
  pinMode(pinGBSerialOut,OUTPUT);

/*
  Set MIDI Serial Rate
*/

#ifdef USE_USB
  serial->begin(31250); //31250
#else
  if(usbMode == true) {
    serial->begin(38400);
  } else {
    pinMode(pinMidiInputPower,OUTPUT);
    digitalWrite(pinMidiInputPower,HIGH); // turn on the optoisolator
    #ifdef USE_LEONARDO
      Serial1.begin(31250); //31250
    #else
      Serial.begin(31250); //31250
    #endif
  }
#endif

/*
  Set Pin States
*/
  digitalWrite(pinGBClock,HIGH);    // gameboy wants a HIGH line
  digitalWrite(pinGBSerialOut,LOW);    // no data to send
/*
  Misc Startup
*/
  keyboardNoteStart = keyboardStartOctave + 12; // Set the octave where the actual notes start (the octave below is for the mutes, cursor, etc)
/*
  Assign the keyboard mode command array for the first octave
*/
  keyboardCommands[0] = keyboardMut1;
  keyboardCommands[1] = keyboardMut2;
  keyboardCommands[2] = keyboardMut3;
  keyboardCommands[3] = keyboardMut4;
  keyboardCommands[4] = keyboardCurL;
  keyboardCommands[5] = keyboardCurR;
  keyboardCommands[6] = keyboardCurU;
  keyboardCommands[7] = keyboardCurD;
  keyboardCommands[8] = keyboardEntr;
  keyboardCommands[9] = keyboardTblDn;
  keyboardCommands[10] = keyboardTblUp;
  keyboardCommands[11] = keyboardTblCue;
/*
  Load Settings from EEPROM
*/
  #ifndef USE_DUE
  if(!memory[MEM_FORCE_MODE]) memory[MEM_MODE] = EEPROM.read(MEM_MODE);
  #endif
  lastMode = memory[MEM_MODE];

/*
  usbMidi sysex support
*/
  usbMidiInit();

  startupSequence();

  showSelectedMode(); //Light up the LED that shows which mode we are in.

  usbMidiStartHost();
}

/*
  Main Loop, which we don't use to be able to isolate each mode into its own setup and loop functions
*/
void loop () {
  setMode();
  switchMode();
}
