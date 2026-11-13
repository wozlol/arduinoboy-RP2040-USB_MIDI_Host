Arduinoboy Host Edition, ready-to-flash firmware

File: Arduinoboy-Host-Edition.uf2
Status: TEST BUILD from the usb-modes-test branch. Built 2026-11-13 from source commit 02d5249. The mode light staying on and the mGB voice lights are tested on hardware. LSDJ modes are not yet tested.

How to install
1. Short the FIRMWARE pads on the board and plug in USB-C.
2. A drive appears on your computer (usually named RPI-RP2). Drag the .uf2 file onto it.
3. The board restarts by itself and the lights run in order.

Built with arduino-pico 5.6.1, MIDI Library 5.0.2 and the edited libraries in Arduinoboy/libraries, board Waveshare RP2040 Zero, 240 MHz, USB Stack Adafruit TinyUSB.
