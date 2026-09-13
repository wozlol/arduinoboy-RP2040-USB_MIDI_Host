#ifdef USE_RP2040
/*
  RP2040 hardware watchdog + breadcrumb diagnostics.

  The RP2040 has a real hardware watchdog timer that runs independently of the CPU. If
  wdFeed() isn't called for longer than the configured timeout, the chip forcibly resets
  itself - no repower needed, unlike whatever is causing the freezes this is chasing.

  wdFeed(phase) is meant to be called from spots that run frequently during normal
  operation, and right before anything that blocks. It stamps two watchdog scratch
  registers (which - unlike ordinary RAM - survive a watchdog-triggered reset) with what
  we were doing and which mode we were in, then feeds the watchdog. If the chip actually
  hangs, whatever was last stamped there is exactly where it happened; wdSetup() reports
  that on the next boot before re-arming.

  Scratch register 4 is used internally by the Pico SDK's watchdog_enable()/
  watchdog_enable_caused_reboot() to mark "this reset came from an armed watchdog, not a
  cold boot or watchdog_reboot()" - left untouched here.

  IMPORTANT: every wdFeed() call site is core0 code. Feeding the watchdog only proves
  core0 is alive - it says nothing about core1, which runs the USB host PIO stack
  (UsbMidi.ino). Confirmed by observation: core1 can die completely (loop1() stops
  advancing core1LoopCount, and the periodic RX-rearm log goes silent forever) while
  core0 keeps cheerfully feeding the watchdog from its own mode loops, so the reset
  never fires. wdFeed() therefore also checks core1LoopCount's own progress here and
  refuses to feed if core1 hasn't completed a loop1() pass in CORE1_LIVENESS_TIMEOUT_MS -
  letting the existing watchdog timeout do a full reset (which restarts core1 too)
  instead of silently tolerating a dead host stack forever.
*/

// Defined in UsbMidi.ino, concatenated after this file by the Arduino build.
extern volatile uint32_t core1LoopCount;

// A healthy loop1() pass completes in well under 1ms, so 400ms is generous margin, not
// a tight guess. Recovery time budget: this + the watchdog's own countdown below is what
// stands between "core1 died" and "chip resets" - kept tight on purpose so the device
// comes back in about 2 seconds total, not the 4-6s an earlier, over-cautious version of
// this took.
constexpr uint32_t CORE1_LIVENESS_TIMEOUT_MS = 400;
uint32_t wdLastCore1LoopCount = 0;
uint32_t wdLastCore1ProgressMs = 0;

boolean wdRecoveringFromStall = false; // set once, read by setup() to skip cosmetic delays

void wdReportAndBlink(uint8_t phase, uint8_t mode)
{
  // Called very early in setup(), before the normal pin-init loop runs - make sure the
  // LEDs are actually usable so this is visible even with no computer attached.
  for(int led=0; led<=5; led++) pinMode(pinLeds[led], OUTPUT);

  Serial.print(F("[watchdog] previous boot hung - last phase="));
  Serial.print(phase);
  Serial.print(F(" mode="));
  Serial.println(mode);
  if(phase == WD_PHASE_CORE1_STALLED) {
    Serial.println(F("[watchdog] core1 (USB host) stopped advancing - core0 was fine, forced the reset itself"));
  }

  // A full multi-second blink-out (the original version of this) is a diagnostic tool
  // for a rare, mysterious hang - it's the wrong trade-off for core1 stalls, which are
  // now an expected, possibly-recurring failure mode we want to recover from fast. Give
  // one quick double-flash acknowledgment instead and rely on the serial log (above) for
  // the detailed phase/mode breadcrumb when someone's actually watching a monitor.
  for(int rep=0; rep<2; rep++) {
    for(int led=0; led<=5; led++) digitalWrite(pinLeds[led], HIGH);
    delay(80);
    for(int led=0; led<=5; led++) digitalWrite(pinLeds[led], LOW);
    delay(80);
  }

  wdRecoveringFromStall = true;
}

void wdSetup()
{
  if(watchdog_caused_reboot() && watchdog_enable_caused_reboot()) {
    wdReportAndBlink((uint8_t)watchdog_hw->scratch[0], (uint8_t)watchdog_hw->scratch[1]);
  }
  // 1.5s: still comfortably above showSelectedMode()'s ~900ms worst-case blink sequence
  // (the longest legitimate blocking call left on core0), but tight enough that recovery
  // from a core1 stall - CORE1_LIVENESS_TIMEOUT_MS to detect it, plus however much of
  // this countdown was already spent - lands around 2 seconds instead of 4-6.
  watchdog_enable(1500, false);
}

void wdFeed(uint8_t phase)
{
  uint32_t nowMs = millis();

  // First call ever (very early in setup(), before core1 has even been told to start
  // via usbMidiStartHost()) starts core1's grace window here rather than at boot time
  // zero, so normal PIO-USB host bring-up latency never looks like a stall.
  if(wdLastCore1ProgressMs == 0) wdLastCore1ProgressMs = nowMs;

  if(core1LoopCount != wdLastCore1LoopCount) {
    wdLastCore1LoopCount = core1LoopCount;
    wdLastCore1ProgressMs = nowMs;
  }

  // The core1-liveness gate exists to recover a USB host port that died mid-use. It must
  // not fire when there's nothing to recover: while a programmer (web/Max editor) is
  // talking to us, a reset would drop that session - and with no USB host device
  // mounted at all, core1 is only idle-scanning the bus, so a stall there isn't worth a
  // full chip reset either. In both cases just keep feeding normally.
  const bool enforceCore1 = (phase != WD_PHASE_PROGRAMMER) && usbMidiHostDeviceMounted();
  if(!enforceCore1) wdLastCore1ProgressMs = nowMs;

  if(enforceCore1 && nowMs - wdLastCore1ProgressMs > CORE1_LIVENESS_TIMEOUT_MS) {
    // core1 hasn't completed a single loop1() pass in CORE1_LIVENESS_TIMEOUT_MS.
    // Stop feeding so the watchdog's own countdown resets the whole chip for us -
    // core0 alone has no way to restart core1 or the PIO-USB host state it owns.
    watchdog_hw->scratch[0] = WD_PHASE_CORE1_STALLED;
    watchdog_hw->scratch[1] = memory[MEM_MODE];
    return;
  }

  watchdog_hw->scratch[0] = phase;
  watchdog_hw->scratch[1] = memory[MEM_MODE];
  watchdog_update();
}
#endif
