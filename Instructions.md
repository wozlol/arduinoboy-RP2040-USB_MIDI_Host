# Arduinoboy Instructions

## Connect
- Link cable port → Game Boy.
- MIDI DIN in/out, or USB-C to a computer (enumerates as MIDI device `Game Boy`), or the host port for a class-compliant USB MIDI keyboard/controller.
- Powered by the Game Boy's link port or by USB.

## Select a Mode
Press the button to cycle modes. There are 6 mode LEDs for 7 modes: modes 1-5 light one LED each, mode 6 blinks two LEDs plus the status LED, and mode 7 lights all six together.

| # | Mode | Game Boy sync setting |
|---|------|------------------------|
| 1 | LSDJ Slave Sync | `Slave` |
| 2 | LSDJ Master Sync | `Master` |
| 3 | LSDJ Keyboard | `Keyboard` |
| 4 | Nanoloop Sync | `slave` (in Nanoloop) |
| 5 | mGB | — (mGB cartridge) |
| 6 | LSDJ MIDIMAP | `MI.MAP` |
| 7 | LSDJ MIDIOUT | `Midiout` |

## Mode 1 — LSDJ Slave Sync
Arduinoboy drives LSDJ's transport and tempo from incoming MIDI.

| Note | Action |
|---|---|
| C-2 (48) | Start |
| C#2 (49) | Stop |
| D-2 (50) | Normal tempo |
| D#2 (51) | 1/2 tempo |
| E-2 (52) | 1/4 tempo |
| F-2 (53) | 1/8 tempo |

Higher notes set LSDJ's song-start row offset.

## Mode 2 — LSDJ Master Sync
LSDJ sends MIDI clock and a note per song row as it plays.

## Mode 3 — LSDJ Keyboard
Emulates LSDJ's PC Keyboard mode.

| Note | Action |
|---|---|
| C-1 (36) | Mute PU1 |
| C#1 (37) | Mute PU2 |
| D-1 (38) | Mute WAV |
| D#1 (39) | Mute NOI |
| E-1 (40) | Live mode cue sequence |
| F-1 (41) | Live mode cursor up |
| F#1 (42) | Live mode cursor down |
| G-1 (43) | Live mode cursor left |
| G#1 (44) | Live mode cursor right |
| A-1 (45) | Table up |
| A#1 (46) | Table down |
| B-1 (47) | Cue table |
| C-2+ (48+) | Play notes |

Program Change messages select the instrument table. Default channel is 16, set in the web/Max editor.

## Mode 4 — Nanoloop Sync
Sends MIDI clock to Nanoloop.

## Mode 5 — mGB
[mGB](https://github.com/trash80/mGB) is a Game Boy cartridge program (needs a flash cart and transfer hardware) that turns the Game Boy into a MIDI synth with full control of its sound hardware. Full MIDI in across all 4 channels, including a 3-voice polyphony channel. Works on DMG and GBC/GBA; on Advance, use a non-Advance Game Boy cart.

Also mirrors MIDI in (serial, USB device, or USB host) to all three MIDI outs as a thru, so Arduinoboy still works as a USB MIDI adapter while in mGB mode.

## Mode 6 — LSDJ MIDIMAP
LSDJ syncs to MIDI clock; incoming notes jump to the matching song row. Requires the MI.MAP build of LSDJ.

## Mode 7 — LSDJ MIDIOUT
Each of the 4 Game Boy channels sends MIDI on its own channel, controlled by LSDJ table/effect commands:

| Command | Effect |
|---|---|
| `Nxx` | Note on/off (`N00` = off, `N01`-`N6F` = notes 1-112) |
| `Qxx` | Note relative to the channel's current pitch |
| `Xxx` | CC (high nibble = CC#, low nibble = value 0-F → 0-127) |
| `Yxx` | Program change |

Requires the Midiout build of LSDJ.

## Editing Settings
Change MIDI channels, boot mode, and other global settings without reflashing:
- [Web Editor](https://woz.lol/arduinoboy/) — Chrome only, no install, connect over MIDI.
- [Max Editor](https://github.com/trash80/Arduinoboy/tree/master/Editor) — needs [Max](https://cycling74.com/downloads/) (a free demo works).

## Updating Firmware
1. Unplug the board.
2. Short the two `FIRMWARE` pads together.
3. While shorted, plug the board into a computer via USB-C.
4. A drive named `RPI-RP2` appears; release the short.
5. Drag `Arduinoboy.ino.uf2` onto it.
6. The board reboots automatically when the copy finishes.
