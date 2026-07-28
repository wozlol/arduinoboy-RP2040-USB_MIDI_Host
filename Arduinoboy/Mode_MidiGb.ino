/**************************************************************************
 * Name:    Timothy Lamb                                                  *
 * Email:   trash80@gmail.com                                             *
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

void modeMidiGbSetup()
{
  digitalWrite(pinStatusLed,LOW);
  pinMode(pinGBClock,OUTPUT);
  digitalWrite(pinGBClock,HIGH);

#ifdef USE_TEENSY
  usbMIDI.setHandleRealTimeSystem(NULL);
#endif

  blinkMaxCount=1000;
  modeMidiGb();
}

void modeMidiGb()
{
  boolean sendByte = false;
#ifdef USE_RP2040
  modeMidiGbResetSerialThru();
#endif
  while(1){                                //Loop foreverrrr
    modeMidiGbUsbMidiReceive();

    if (serial->available()) {          //If MIDI is sending
      incomingMidiByte = serial->read();    //Get the byte sent from MIDI

      checkForProgrammerSysex(incomingMidiByte);
      if(!usbMode) {
        serial->write(incomingMidiByte); // In MGB mode every DIN byte is also sent to MIDI Out.
      }
#ifdef USE_RP2040
      modeMidiGbThruSerialByte(incomingMidiByte);
#endif

      if(incomingMidiByte & 0x80) {
        switch (incomingMidiByte & 0xF0) {
          case 0xF0:
            midiValueMode = false;
            break;
          default:
            sendByte = false;
            midiStatusChannel = incomingMidiByte&0x0F;
            midiStatusType    = incomingMidiByte&0xF0;
            if(midiStatusChannel == memory[MEM_MGB_CH]) {
               midiData[0] = midiStatusType;
               sendByte = true;
            } else if (midiStatusChannel == memory[MEM_MGB_CH+1]) {
               midiData[0] = midiStatusType+1;
               sendByte = true;
            } else if (midiStatusChannel == memory[MEM_MGB_CH+2]) {
               midiData[0] = midiStatusType+2;
               sendByte = true;
            } else if (midiStatusChannel == memory[MEM_MGB_CH+3]) {
               midiData[0] = midiStatusType+3;
               sendByte = true;
            } else if (midiStatusChannel == memory[MEM_MGB_CH+4]) {
               midiData[0] = midiStatusType+4;
               sendByte = true;
            } else {
              midiValueMode  =false;
              midiAddressMode=false;
            }
            if(sendByte) {
              statusLedOn();
              sendByteToGameboy(midiData[0]);
              delayMicroseconds(GB_MIDI_DELAY);
              midiValueMode  =false;
              midiAddressMode=true;
            }
           break;
        }
      } else if (midiAddressMode){
        midiAddressMode = false;
        midiValueMode = true;
        midiData[1] = incomingMidiByte;
        sendByteToGameboy(midiData[1]);
        delayMicroseconds(GB_MIDI_DELAY);
      } else if (midiValueMode) {
        midiData[2] = incomingMidiByte;
        midiAddressMode = true;
        midiValueMode = false;

        sendByteToGameboy(midiData[2]);
        delayMicroseconds(GB_MIDI_DELAY);
        statusLedOn();
        blinkLight(midiData[0],midiData[2]);
      }
    } else {
      setMode();                // Check if mode button was depressed
      updateBlinkLights();
      updateStatusLed();
    }
  }
}

 /*
 sendByteToGameboy does what it says. yay magic
 */
void sendByteToGameboy(byte send_byte)
{
 for(countLSDJTicks=0;countLSDJTicks!=8;countLSDJTicks++) {  //we are going to send 8 bits, so do a loop 8 times
   if(send_byte & 0x80) {
       GB_SET(0,1,0);
       GB_SET(1,1,0);
   } else {
       GB_SET(0,0,0);
       GB_SET(1,0,0);
   }

#if defined (F_CPU) && (F_CPU > 24000000)
   // Delays for Teensy etc where CPU speed might be clocked too fast for cable & shift register on gameboy.
   delayMicroseconds(1);
#endif
   send_byte <<= 1;
 }
}

#ifdef USE_RP2040

uint8_t mgbThruRunningStatus = 0;
uint8_t mgbThruStatus = 0;
uint8_t mgbThruData[2] = {0, 0};
uint8_t mgbThruDataCount = 0;
uint8_t mgbThruDataLength = 0;
boolean mgbThruSysex = false;
uint8_t mgbThruSysexData[3] = {0, 0, 0};
uint8_t mgbThruSysexCount = 0;

void modeMidiGbResetSerialThru()
{
  mgbThruRunningStatus = 0;
  mgbThruStatus = 0;
  mgbThruDataCount = 0;
  mgbThruDataLength = 0;
  mgbThruSysex = false;
  mgbThruSysexCount = 0;
}

uint8_t modeMidiGbSerialDataLength(uint8_t status)
{
  if(status < 0xF0) {
    return ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1 : 2;
  }

  switch(status) {
    case 0xF1:
    case 0xF3:
      return 1;
    case 0xF2:
      return 2;
    default:
      return 0;
  }
}

void modeMidiGbThruSerialMessage()
{
  const uint8_t length = mgbThruDataLength + 1;
  usbMidiMgbThruToUsb(
    usbMidiCodeIndex(mgbThruStatus, length),
    mgbThruStatus,
    mgbThruData[0],
    mgbThruData[1]
  );
  mgbThruDataCount = 0;

  if(mgbThruStatus >= 0xF0) {
    mgbThruStatus = 0;
    mgbThruDataLength = 0;
  }
}

void modeMidiGbThruSerialByte(uint8_t value)
{
  // Convert the DIN byte stream to USB-MIDI packets while preserving running status.
  if(value >= 0xF8) {
    usbMidiMgbThruToUsb(0x0F, value, 0, 0);
    return;
  }

  if(value == 0xF0) {
    mgbThruRunningStatus = 0;
    mgbThruStatus = 0;
    mgbThruSysex = true;
    mgbThruSysexCount = 0;
  }

  if(mgbThruSysex) {
    if(mgbThruSysexCount < sizeof(mgbThruSysexData)) {
      mgbThruSysexData[mgbThruSysexCount++] = value;
    }

    if(mgbThruSysexCount == sizeof(mgbThruSysexData) || value == 0xF7) {
      const uint8_t cin = value == 0xF7 ? 0x04 + mgbThruSysexCount : 0x04;
      usbMidiMgbThruToUsb(
        cin,
        mgbThruSysexData[0],
        mgbThruSysexCount > 1 ? mgbThruSysexData[1] : 0,
        mgbThruSysexCount > 2 ? mgbThruSysexData[2] : 0
      );
      mgbThruSysexCount = 0;
    }
    if(value == 0xF7) mgbThruSysex = false;
    return;
  }

  if(value & 0x80) {
    mgbThruDataCount = 0;
    if(value < 0xF0) {
      mgbThruRunningStatus = value;
      mgbThruStatus = value;
    } else {
      mgbThruRunningStatus = 0;
      mgbThruStatus = value;
    }
    mgbThruDataLength = modeMidiGbSerialDataLength(value);

    if(mgbThruDataLength == 0 && value != 0xF0 && value != 0xF7) {
      usbMidiMgbThruToUsb(0x05, value, 0, 0);
      mgbThruStatus = 0;
    }
    return;
  }

  if(mgbThruStatus == 0 && mgbThruRunningStatus != 0) {
    mgbThruStatus = mgbThruRunningStatus;
    mgbThruDataLength = modeMidiGbSerialDataLength(mgbThruStatus);
  }
  if(mgbThruStatus == 0 || mgbThruDataCount >= sizeof(mgbThruData)) return;

  mgbThruData[mgbThruDataCount++] = value;
  if(mgbThruDataCount == mgbThruDataLength) modeMidiGbThruSerialMessage();
}

#endif

void modeMidiGbUsbMidiReceive()
{
#ifdef USE_RP2040
    UsbMidiMessage rx;
    while(usbMidiReadMessage(&rx)) {
        // Only MGB mode mirrors every received USB message to DIN, USB device,
        // and every connected USB host MIDI output, making this a USB MIDI adapter.
        usbMidiMgbThruToAll(&rx);
        if(rx.status >= 0xF0 || (rx.cin >= 0x04 && rx.cin <= 0x07)) continue;

        uint8_t ch = rx.status & 0x0F;
        boolean send = false;
        if(ch == memory[MEM_MGB_CH]) {
            ch = 0;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+1]) {
            ch = 1;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+2]) {
            ch = 2;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+3]) {
            ch = 3;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+4]) {
            ch = 4;
            send = true;
        }
        if(!send) continue;

        uint8_t s;
        switch(rx.status & 0xF0) {
            case 0x80:
            case 0x90:
                s = (rx.status & 0xF0) + ch;
                sendByteToGameboy(s);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(rx.data1);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(rx.data2);
                delayMicroseconds(GB_MIDI_DELAY);
                blinkLight(s, rx.data2);
                break;
            case 0xB0:
                sendByteToGameboy(0xB0 + ch);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(rx.data1);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(rx.data2);
                delayMicroseconds(GB_MIDI_DELAY);
                blinkLight(0xB0 + ch, rx.data2);
                break;
            case 0xC0:
                sendByteToGameboy(0xC0 + ch);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(rx.data1);
                delayMicroseconds(GB_MIDI_DELAY);
                blinkLight(0xC0 + ch, rx.data1);
                break;
            case 0xE0:
                sendByteToGameboy(0xE0 + ch);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(rx.data1);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(rx.data2);
                delayMicroseconds(GB_MIDI_DELAY);
                break;
            default:
                continue;
        }
        statusLedOn();
    }
#endif

#ifdef USE_TEENSY

    while(usbMIDI.read()) {
        uint8_t ch = usbMIDI.getChannel() - 1;
        boolean send = false;
        if(ch == memory[MEM_MGB_CH]) {
            ch = 0;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+1]) {
            ch = 1;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+2]) {
            ch = 2;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+3]) {
            ch = 3;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+4]) {
            ch = 4;
            send = true;
        }
        if(!send) return;
        uint8_t s;
        switch(usbMIDI.getType()) {
            case 0x80: // note off
            case 0x90: // note on
                s = 0x90 + ch;
                if(usbMIDI.getType() == 0x80) {
                    s = 0x80 + ch;
                }
                sendByteToGameboy(s);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(usbMIDI.getData1());
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(usbMIDI.getData2());
                delayMicroseconds(GB_MIDI_DELAY);
                blinkLight(s, usbMIDI.getData2());
            break;
            case 0xB0: // CC
                sendByteToGameboy(0xB0+ch);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(usbMIDI.getData1());
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(usbMIDI.getData2());
                delayMicroseconds(GB_MIDI_DELAY);
                blinkLight(0xB0+ch, usbMIDI.getData2());
            break;
            case 0xC0: // PG
                sendByteToGameboy(0xC0+ch);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(usbMIDI.getData1());
                delayMicroseconds(GB_MIDI_DELAY);
                blinkLight(0xC0+ch, usbMIDI.getData2());
            break;
            case 0xE0: // PB
                sendByteToGameboy(0xE0+ch);
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(usbMIDI.getData1());
                delayMicroseconds(GB_MIDI_DELAY);
                sendByteToGameboy(usbMIDI.getData2());
                delayMicroseconds(GB_MIDI_DELAY);
            break;
        }

        statusLedOn();
    }
#endif

#ifdef USE_LEONARDO

    midiEventPacket_t rx;
      do
      {
        rx = MidiUSB.read();
        uint8_t ch = rx.byte1 & 0x0F;
        boolean send = false;
        if(ch == memory[MEM_MGB_CH]) {
            ch = 0;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+1]) {
            ch = 1;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+2]) {
            ch = 2;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+3]) {
            ch = 3;
            send = true;
        } else if (ch == memory[MEM_MGB_CH+4]) {
            ch = 4;
            send = true;
        }
        if (!send) return;
        uint8_t s;
        switch (rx.header)
        {
        case 0x08: // note off
        case 0x09: // note on
          s = 0x90 + ch;
          if (rx.header == 0x08)
          {
            s = 0x80 + ch;
          }
          sendByteToGameboy(s);
          delayMicroseconds(GB_MIDI_DELAY);
          sendByteToGameboy(rx.byte2);
          delayMicroseconds(GB_MIDI_DELAY);
          sendByteToGameboy(rx.byte3);
          delayMicroseconds(GB_MIDI_DELAY);
          blinkLight(s, rx.byte2);
          break;
        case 0x0B: // CC
          sendByteToGameboy(0xB0 + ch);
          delayMicroseconds(GB_MIDI_DELAY);
          sendByteToGameboy(rx.byte2);
          delayMicroseconds(GB_MIDI_DELAY);
          sendByteToGameboy(rx.byte3);
          delayMicroseconds(GB_MIDI_DELAY);
          blinkLight(0xB0 + ch, rx.byte2);
          break;
        case 0x0C: // PG
          sendByteToGameboy(0xC0 + ch);
          delayMicroseconds(GB_MIDI_DELAY);
          sendByteToGameboy(rx.byte2);
          delayMicroseconds(GB_MIDI_DELAY);
          blinkLight(0xC0 + ch, rx.byte2);
          break;
        case 0x0E: // PB
          sendByteToGameboy(0xE0 + ch);
          delayMicroseconds(GB_MIDI_DELAY);
          sendByteToGameboy(rx.byte2);
          delayMicroseconds(GB_MIDI_DELAY);
          sendByteToGameboy(rx.byte3);
          delayMicroseconds(GB_MIDI_DELAY);
          break;
        default:
          return;
        }

        statusLedOn();
      } while (rx.header != 0);
#endif
}
