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

//LSDj's phrase editor remembers one held PS/2 note. Release it before
//changing octave: otherwise the same scan code can refer to a different note.
byte keyboardHeldMidiNote = 0xFF;
byte keyboardHeldScanCode = 0;
byte keyboardMidiStatus = 0;
byte keyboardMidiNote = 0;
bool keyboardMidiHasNote = false;

void releaseLSDJKeyboardNote()
{
  if(keyboardHeldMidiNote == 0xFF) return;
  sendKeyboardByteToGameboy(0xF0); //PS/2 set 2 break prefix
  sendKeyboardByteToGameboy(keyboardHeldScanCode);
  keyboardHeldMidiNote = 0xFF;
}

//Keep Note Off running status separate from Note On, and do not use note
//number zero to mean "we haven't received a note yet". Real-time bytes may appear
//between either data byte; system-common messages cancel running status.
void handleLSDJKeyboardMidiByte(byte value)
{
  if(value >= 0xF8) return;
  //If the byte is a Status Message
  if(value & 0x80) {
    /* Status message Information (# = midi channel 0 to F [1-16] )
        0x8# = Note Off
        0x9# = Note On
        0xA# = AfterTouch (ie, key pressure)
        0xB# = Control Change
        0xC# = Program (patch) change
        0xD# = Channel Pressure
        0xE# = Pitch Wheel
        0xF0 - 0xF7 = System Common Messages
        0xF8 - 0xFF = System Realtime Messages
    */
    keyboardMidiStatus = value < 0xF0 ? value : 0;
    keyboardMidiHasNote = false;
    return;
  }

  //Weee hello world bitwise and. ... make the second hex digit zero so we can check the message type
  // - the second digit is usually the midi channel 0 to F (1-16) unless its a 0xF0 message...
  const byte type = keyboardMidiStatus & 0xF0;
  //Set the number to a "note on" message so we can use the same "channel" variable as note on messages
  const byte channel = 0x90 | (keyboardMidiStatus & 0x0F);
  if(type == 0xC0) {
    //Program change message
    changeLSDJInstrument(channel, value);
    if(channel == (0x90 + memory[MEM_KEYBD_CH])) statusLedOn();
  } else if(type == 0x80 || type == 0x90) {
    //Note-On Status Message (Note: we have to treat this carefully because note status isnt sent on every note-on, damn it)
    //There are 3 bytes total we need: Channel, Note, and velocity. Save the status and note until we have the velocity,
    //at that point we can then call our note out function to LSDJ
    //Note-Off messages use the same layout, but pass velocity zero to release the key.
    if(!keyboardMidiHasNote) {
      //If we dont have a note number, we assume this byte is the note number, get it...
      keyboardMidiNote = value;
      keyboardMidiHasNote = true;
    } else {
      //We have our note and channel, so call our note function...
      playLSDJNote(channel, keyboardMidiNote, type == 0x80 ? 0 : value);
      if(channel == (0x90 + memory[MEM_KEYBD_CH])) statusLedOn();
      keyboardMidiHasNote = false; //Set the note capture state to false, forcing to capture the next note
    }
  }
}

void modeLSDJKeyboardSetup()
{
  ledRest(5);
  pinMode(pinGBClock,OUTPUT);
  digitalWrite(pinGBClock,HIGH);
 #ifdef USE_TEENSY
  usbMIDI.setHandleRealTimeSystem(NULL);
 #endif
  blinkMaxCount=1000;

  /* The stuff below makes sure the code is in the same state as LSDJ on reset / restart, mode switched, etc. */

  releaseLSDJKeyboardNote();
  keyboardMidiStatus = 0;
  keyboardMidiHasNote = false;

  for(int rst=0;rst<5;rst++) sendKeyboardByteToGameboy(keyboardOctDn);   //Return lsdj to the first octave
  for(int rst=0;rst<41;rst++) sendKeyboardByteToGameboy(keyboardInsDn);  //Return lsdj to the first instrument

  keyboardCurrentOct = 0;  //Set our current octave to 0.
  keyboardLastOct    = 0;  //Set our last octave to 0.
  keyboardCurrentIns = 0;  //Set out current instrument to 0.
  keyboardLastIns    = 0;  //Set out last used instrument to 0.


  modeLSDJKeyboard();      //.... And start the fun
}


void modeLSDJKeyboard()
{
  while(1){                              //Loop foreverrrr
  #ifdef USE_RP2040
  wdFeed(WD_PHASE_MODE_LOOP);
  #endif
  modeLSDJKeyboardMidiReceive();
  if (serial->available()) {          //If MIDI is sending
    incomingMidiByte = serial->read();    //Get the byte sent from MIDI
    if(!checkForProgrammerSysex(incomingMidiByte) && !usbMode) serial->write(incomingMidiByte);//Echo the Byte to MIDI Output


    /***************************************************************************
     * Midi to LSDJ Keyboard Handling                                          *
     ***************************************************************************/
    handleLSDJKeyboardMidiByte(incomingMidiByte);
  }

  updateStatusLed();        // Update our status blinker
  setMode();                // Check if mode button was depressed
  }
}


 /*
  changeLSDJInstrument does what it does via magic (rtfm, realize the fucking magic)
 */
void changeLSDJInstrument(byte channel,byte message)
{
  keyboardCurrentIns = message; //set the current instrument number

  if(channel == (0x90+memory[MEM_KEYBD_CH]) && keyboardCurrentIns != keyboardLastIns) {
  //if its on our midi channel and the instrument isnt the same as our currrent
    if(!memory[MEM_KEYBD_COMPAT_MODE]) {
      sendKeyboardByteToGameboy(0x80 | message); // <- this is suppose to work but doesn't :/
    } else {
      //We will find out which is greater, the current instrument or the last instrument. then
      //cycle up or down to that instrument
      if(keyboardCurrentIns > keyboardLastIns) {
        keyboardDiff = keyboardCurrentIns - keyboardLastIns;
        for(keyboardCount=0;keyboardCount<keyboardDiff;keyboardCount++) {
          sendKeyboardByteToGameboy(keyboardInsUp);  //send the instrument up command to go up to the requested instrument
        }
      } else {
        keyboardDiff = keyboardLastIns - keyboardCurrentIns;
        for(keyboardCount=0;keyboardCount<keyboardDiff;keyboardCount++) {
          sendKeyboardByteToGameboy(keyboardInsDn);  //send the instrument down command to down to the requested instrument
        }
      }
    }
    keyboardLastIns = keyboardCurrentIns; //assign our instrument for future reference to this function
  }
}


 /*
  w00t...
 */
void playLSDJNote(byte channel,byte note, byte velocity)
{
  if(channel != (0x90+memory[MEM_KEYBD_CH])) return;
  if(velocity == 0) {
    //Late releases of older, overlapping notes must not release the new note
    //or move LSDj back to the older note's octave.
    if(note == keyboardHeldMidiNote) releaseLSDJKeyboardNote();
    return;
  }
  if(note >= keyboardNoteStart) {
    //Also handle another Note On for the same pitch without an intervening
    //Note Off. LSDj treats repeated make codes as one held keyboard key.
    releaseLSDJKeyboardNote();
    keyboardHeldMidiNote = note;
    keyboardNoteOffset = 0;
    note = note - keyboardNoteStart;  //subtract the octave offset to get a value ranging from 0 to 48 for comparison

    keyboardCurrentOct = note / 0x0C;  //get a octave value from 0 to 4 by deviding the current note by 12
    changeLSDJOctave();

    if(note >= 0x3C) keyboardNoteOffset = 0x0C; //if the note really high we need to use the second row of keyboard keys
    note = (note % 12) + keyboardNoteOffset;    //get a 0 to 11 range of notes and add the offset
    keyboardHeldScanCode = keyboardNotes[note];
    sendKeyboardByteToGameboy(keyboardHeldScanCode);       // and finally send the note

  } else if (note >= keyboardStartOctave) { //If we are at the octave below notes
    keyboardDiff = note - keyboardStartOctave; //Get a value between 0 and 11
    if(keyboardDiff < 8 && keyboardDiff > 3) sendKeyboardByteToGameboy(0xE0); //if we are sending cursor values we have to send a 0xE0 byte for "extended" pc keyboard mode
    sendKeyboardByteToGameboy(keyboardCommands[note - keyboardStartOctave]); //send the byte corrisponding to the note number in the keyboard command array
  }
}


 /*
  changeLSDJOctave compares the last octave with the current one and then sends a byte
  to shift the octave to match if need be. its pretty much the same as the changeLSDJInstrument function.
 */
void changeLSDJOctave()
{
  if(keyboardCurrentOct != keyboardLastOct) {
    if(!memory[MEM_KEYBD_COMPAT_MODE]) { // This new mode doesnt work yet. :/
      keyboardCurrentOct = 0xB3 + keyboardCurrentOct;
      sendKeyboardByteToGameboy(keyboardCurrentOct);
    } else {
      ///We will find out which is greater, the current octave or the last. then
      //cycle up or down to that octave
      if(keyboardCurrentOct > keyboardLastOct) {
        keyboardDiff = keyboardCurrentOct - keyboardLastOct;
        for(keyboardCount=0;keyboardCount<keyboardDiff;keyboardCount++)
        {
           sendKeyboardByteToGameboy(keyboardOctUp);
        }
      } else {
        keyboardDiff = keyboardLastOct - keyboardCurrentOct;
        for(keyboardCount=0;keyboardCount<keyboardDiff;keyboardCount++)
        {
          sendKeyboardByteToGameboy(keyboardOctDn);
        }
      }
    }
    keyboardLastOct = keyboardCurrentOct;
  }
}


/*
  sendKeyboardByteToGameboy
*/
void sendKeyboardByteToGameboy(byte send_byte)
{
  GB_SET(0,0,0);
  delayMicroseconds(50);  //Delays are added to simulate PC keyboard rate
  GB_SET(1,0,0);
  delayMicroseconds(50);
  for(countLSDJTicks=0;countLSDJTicks<8;countLSDJTicks++) {  //we are going to send 8 bits, so do a loop 8 times
    if(send_byte & 0x01) {          //if the first bit is equal to 1
      GB_SET(0,1,0);
      delayMicroseconds(50);
      GB_SET(1,1,0);
      delayMicroseconds(50);
    } else {
      GB_SET(0,0,0);
      delayMicroseconds(50);
      GB_SET(1,0,0);
      delayMicroseconds(50);
    }
    send_byte >>= 1;                //bitshift right once for the next bit we are going to send
  }

  GB_SET(0,0,0);
  delayMicroseconds(50);
  GB_SET(1,0,0);
  delayMicroseconds(50);
  GB_SET(0,1,0);
  delayMicroseconds(50);
  GB_SET(1,1,0);
  delayMicroseconds(4000);
}

void modeLSDJKeyboardMidiReceive()
{
#ifdef USE_RP2040
    UsbMidiMessage rx;
    while(usbMidiReadMessage(&rx)) {
      const uint8_t ch = rx.status & 0x0F;
      if(ch != memory[MEM_KEYBD_CH]) continue;

      switch(rx.status & 0xF0) {
        case 0x80:
          playLSDJNote(0x90 + memory[MEM_KEYBD_CH], rx.data1, 0);
          statusLedOn();
          break;
        case 0x90:
          playLSDJNote(0x90 + memory[MEM_KEYBD_CH], rx.data1, rx.data2);
          statusLedOn();
          break;
        case 0xC0:
          changeLSDJInstrument(0xC0 + memory[MEM_KEYBD_CH], rx.data1);
          statusLedOn();
          break;
      }
    }
#endif

#ifdef USE_TEENSY

    while(usbMIDI.read(memory[MEM_KEYBD_CH]+1)) {
        switch(usbMIDI.getType()) {
            case 0x80: // note off
                playLSDJNote(0x90+memory[MEM_KEYBD_CH], usbMIDI.getData1(), 0);
            break;
            case 0x90: // note on
                playLSDJNote(0x90+memory[MEM_KEYBD_CH], usbMIDI.getData1(), usbMIDI.getData2());
            break;
            case 0xC0: // PG
                changeLSDJInstrument(0xC0+memory[MEM_KEYBD_CH], usbMIDI.getData1());
            break;
            /*
            case 3: // CC
            break;
            case 5: // AT
            break;
            case 6: // PB
            break;
            */
        }
    }
#endif
#ifdef USE_LEONARDO

    midiEventPacket_t rx;
    do {
      rx = MidiUSB.read();
      switch (rx.header)
        {
        case 0x08: // note off
          playLSDJNote(0x90+memory[MEM_KEYBD_CH], rx.byte2, 0);
          statusLedOn();
          break;
        case 0x09: // note on
          playLSDJNote(0x90+memory[MEM_KEYBD_CH], rx.byte2, rx.byte3);
          statusLedOn();
          break;
        case 0x0C: // PG
          changeLSDJInstrument(0xC0+memory[MEM_KEYBD_CH], rx.byte2);
          statusLedOn();
          break;
      }
    } while (rx.header != 0);
#endif
}
