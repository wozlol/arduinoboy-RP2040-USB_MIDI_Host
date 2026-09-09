
#ifdef USE_RP2040
// Defined in UsbMidi.ino, which is concatenated after this file by the Arduino build.
extern volatile uint32_t core1LoopCount;
extern volatile uint8_t core1State;

/*
  EEPROM.commit() calls rp2040.idleOtherCore(), which busy-waits with NO timeout for
  core1 to acknowledge a doorbell interrupt before it's safe to erase/program flash.
  core1 runs the USB host PIO stack (see UsbMidi.ino), which has been observed to stall
  for multi-second stretches even with nothing attached to the host port. If that stall
  lines up with a mode-button press (which commits to flash on every press), core0 hangs
  until core1 happens to come back - explaining a freeze that self-recovers after a few
  seconds instead of needing a repower.

  This wrapper refuses to make that unbounded bet: it first confirms core1 has completed
  at least one loop1() pass within 50ms (proof it's alive, not just "not yet detected as
  stuck"), and skips the flash write instead of blocking forever if it hasn't. A skipped
  write just means this particular mode change doesn't persist across a power cycle -
  far better than freezing the whole device. Every outcome is logged over the serial/CDC
  port (already required by the TinyUSB stack) so a freeze can be correlated with what
  core1 was doing at the time.
*/
boolean commitMemoryToFlash()
{
  wdFeed(WD_PHASE_COMMIT_WAIT);
  uint32_t startLoops = core1LoopCount;
  uint32_t startTime = millis();
  while(core1LoopCount == startLoops) {
    if(millis() - startTime > 50) {
      Serial.print(F("[flash] SKIPPED commit - core1 unresponsive for 50ms, core1State="));
      Serial.println(core1State);
      return false;
    }
  }
  uint32_t waitedMs = millis() - startTime;
  wdFeed(WD_PHASE_EEPROM_COMMIT);
  EEPROM.commit();
  Serial.print(F("[flash] commit ok, waited "));
  Serial.print(waitedMs);
  Serial.println(F("ms for core1"));
  return true;
}
#endif

boolean checkMemory()
{
  byte chk;
  #ifndef USE_DUE
  for(int m=0;m<4;m++){
    chk =  EEPROM.read(MEM_CHECK+m);
    if(chk != defaultMemoryMap[MEM_CHECK+m]) {
      return false;
    }
  }
  #endif
  return true;
}

void initMemory(boolean reinit)
{
  if(!alwaysUseDefaultSettings) {
    #ifndef USE_DUE
    if(reinit || !checkMemory()) {
      for(int m=(MEM_MAX - 1);m>=0;m--){
        EEPROM.write(m,defaultMemoryMap[m]);
      }
      #ifdef USE_RP2040
      commitMemoryToFlash();
      #endif
    }
    #endif
    loadMemory();
  } else {
    for(int m=0;m<MEM_MAX;m++){
      memory[m] = defaultMemoryMap[m];
    }
  }
  changeTasks();
}


void loadMemory()
{
  #ifndef USE_DUE
  for(int m=(MEM_MAX - 1);m>=0;m--){
     memory[m] = EEPROM.read(m);
  }
  #endif
  changeTasks();
}

void printMemory()
{
  for(int m=0;m<MEM_MAX;m++){
    serial->println(memory[m],HEX);
  }
}

void saveMemory()
{
  #ifndef USE_DUE
  for(int m=(MEM_MAX-1);m>=0;m--){
    EEPROM.write(m,memory[m]);
  }
  #ifdef USE_RP2040
  commitMemoryToFlash();
  #endif
  changeTasks();
  #endif
}

void changeTasks()
{
  midioutByteDelay = memory[MEM_MIDIOUT_BYTE_DELAY] * memory[MEM_MIDIOUT_BYTE_DELAY+1];
  midioutBitDelay = memory[MEM_MIDIOUT_BIT_DELAY] * memory[MEM_MIDIOUT_BIT_DELAY+1];
}
