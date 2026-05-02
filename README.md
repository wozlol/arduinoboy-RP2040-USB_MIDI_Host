# Arduinoboy

Version: 1.3.5 - RP2040 USB MIDI device and host update
Date: April 29 2026
Name: Joseph Wozniak
Email: woz@woz.lol

Adds RP2040 / Arduino-Pico support, built-in USB MIDI device support, and PIO-USB host support for class-compliant USB MIDI keyboards.
Previous version: 1.3.4 - Modified by Entropy Electronics for ProMicro
Date: May 19 2020
Original author: Timothy Lamb
Email: trash80@gmail.com

ORIGINAL:
https://github.com/trash80/Arduinoboy

## RP2040 / Arduino-Pico Pin Settings

- 6 LEDS on GPIO pins 9,10,11,12,13,16
- Push button on GPIO pin 5 (for selecting mode)
- Gameboy Clock line on GPIO pin 6
- Gameboy Serial Data input on GPIO pin 7
- Serial Data from gameboy on GPIO pin 8
- USB MIDI host D+ to 22ohm resistor to GPIO pin 14
- USB MIDI host D- to 22ohm resistor to GPIO pin 15
- USB MIDI host port requires 5V VBUS power, and two 15k resistors, one from D+ to ground, and another from D- to ground.

USB HOST NOTES:
- IMPORTANT: set CPU Speed: "240 MHz (Overclock)" for maximum MIDI controller compatibility
- use a ground wire to the usb host thicker than 30awg.
- Twist D+ and D-, try to keep them short.
- The 22ohm resistors go near the RP2040.

**RP2040 built-in USB works as a USB MIDI device.**
**RP2040 GPIO14/GPIO15 port works as a USB MIDI host.**

## Program Information

mGB - MIDI channels changed so 3-note poly is ch1, as it should be, for maximum gladness.

LSDJ Slave Mode Midi Note Effects:

- 48 - C-2 Sends a Sequencer Start Command
- 49 - C#2 Sends a Sequencer Stop Command
- 50 - D-2 Toggles Normal Tempo
- 51 - D#2 Toggles 1/2 Tempo
- 52 - E-2 Toggles 1/4 Tempo
- 53 - F-2 Toggles 1/8 Tempo

LSDJ Keyboard Mode:

- 48 - C-2 Mute Pu1 Off/On
- 49 - C#2 Mute Pu2 Off/On
- 50 - D-2 Mute Wav Off/On
- 51 - D#2 Mute Noi Off/On
- 52 - E-2 Livemode Cue Sequence
- 53 - F-2 Livemode Cursor Up
- 54 - F#2 Livemode Cursor Down
- 55 - G-2 Livemode Cursor Left
- 56 - G#2 Livemode Cursor Right
- 57 - A-2 Table Up
- 58 - A#2 Table Down
- 59 - B-2 Cue Table
- 60 - C-3 to C-8 Notes!
- Prgram Change to select from instrument table

This code should still be compatible with the legacy microcontrollers below
**## Arduino Pin Settings

- 6 LEDS on pins 8 to 13
- Push button on pin 3 (for selecting mode)
- MIDI Opto-isolator power on pin 4
- Gameboy Clock line on analog in pin 0
- Gameboy Serial Data input on analog in pin 1
- Serial Data from gameboy on analog in pin 2

## Teensy Pin Settings

- 6 LEDS on pins 23,22,21,20,4,13
- Push button on pin 2 (for selecting mode)
- MIDI Opto-isolator power connected to +3v
- Gameboy Clock line on pin 16
- Gameboy Serial Data input on pin 17
- Serial Data from gameboy on pin 18

Teensy USB MIDI is supported.

Teensy LC should work but untested.**
