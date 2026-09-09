#if defined(USE_RP2040)

/*
  RP2040 USB MIDI support added by woz.lol.

  The built-in USB port enumerates as a USB MIDI device. A second USB port
  on GPIO14/GPIO15 is used as a PIO-USB host for class-compliant USB MIDI
  keyboards. Wire GPIO14 to host D+, GPIO15 to host D-, and provide 5V VBUS
  power to the hosted USB port.
*/

#ifndef USB_MIDI_MESSAGE_DEFINED
#define USB_MIDI_MESSAGE_DEFINED 1
struct UsbMidiMessage {
  uint8_t cin;
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
  uint8_t length;
};
#endif

constexpr uint8_t PIN_USB_HOST_DP = 14;
constexpr uint8_t PIN_USB_HOST_DM = 15;
constexpr uint8_t PIN_USB_HOST_VBUS_EN = 255;
constexpr uint8_t USB_MIDI_QUEUE_SIZE = 64;
constexpr uint8_t USB_MIDI_HOST_TX_QUEUE_SIZE = 64;
constexpr uint8_t MAX_USB_MIDI_DEVICES = 8;

Adafruit_USBD_MIDI usb_midi;
Adafruit_USBH_Host USBHost;

volatile bool usbHostStartRequested = false;
volatile uint8_t usbMidiHead = 0;
volatile uint8_t usbMidiTail = 0;
UsbMidiMessage usbMidiQueue[USB_MIDI_QUEUE_SIZE];
volatile uint8_t usbMidiHostTxHead = 0;
volatile uint8_t usbMidiHostTxTail = 0;
uint8_t usbMidiHostTxQueue[USB_MIDI_HOST_TX_QUEUE_SIZE][4];
bool usbMidiDeviceStarted = false;

/*
  core1 health tracking, used by commitMemoryToFlash() (Memory_Functions.ino) to avoid
  blocking forever in rp2040.idleOtherCore() when core1 (running the USB host PIO stack)
  is stuck mid-transaction. core1LoopCount ticks once per loop1() pass; core1State says
  what it was doing last. Single-word/byte writes are atomic across RP2040 cores, so no
  locking is needed for either.
*/
volatile uint32_t core1LoopCount = 0;
volatile uint8_t core1State = 0; // 0=not started, 1=idle/draining queues, 2=in USBHost.task(), 3=mount cb, 4=unmount cb

struct UsbMidiDevice {
  bool mounted;
  uint8_t idx;
  uint8_t rxCableCount;
  uint8_t txCableCount;
  uint32_t mountedMs;
  uint32_t lastRxMs;    // 0 until the first packet ever arrives from this device
  uint32_t lastRearmMs; // 0 until the first forced rearm
};

UsbMidiDevice usbMidiDevices[MAX_USB_MIDI_DEVICES];

// A mounted device's RX bulk-IN transfer is normally "busy" (pending) essentially all
// the time by design - TinyUSB resubmits it the instant the previous one completes, so
// that alone can't distinguish "quietly waiting for the next note" from "stuck forever
// on a transfer that will never complete". USB_MIDI_RX_SILENCE_MS is how long we let a
// mounted device go without producing a single byte before we stop assuming it's just
// idle and force tuh_midi_rx_rearm() to abort + resubmit that pending transfer. See
// tuh_midi_rx_rearm() in Adafruit_TinyUSB_Library src/class/midi/midi_host.c - it's a
// real, already-available function, not something added here.
constexpr uint32_t USB_MIDI_RX_SILENCE_MS = 2000;

extern "C" bool tuh_midi_rx_rearm(uint8_t idx);

uint8_t usbMidiMessageLength(uint8_t status)
{
  if(status >= 0xF8) return 1;
  switch(status & 0xF0) {
    case 0xC0:
    case 0xD0:
      return 2;
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
      return 3;
    default:
      return 0;
  }
}

uint8_t usbMidiCinLength(uint8_t cin)
{
  switch(cin) {
    case 0x5:
    case 0xF:
      return 1;
    case 0x2:
    case 0x6:
    case 0xC:
    case 0xD:
      return 2;
    case 0x3:
    case 0x4:
    case 0x7:
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xE:
      return 3;
    default:
      return 0;
  }
}

bool usbMidiEnqueue(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint8_t length)
{
  noInterrupts();
  const uint8_t next = (usbMidiHead + 1) % USB_MIDI_QUEUE_SIZE;
  if(next == usbMidiTail) {
    interrupts();
    return false;
  }

  usbMidiQueue[usbMidiHead].cin = cin;
  usbMidiQueue[usbMidiHead].status = status;
  usbMidiQueue[usbMidiHead].data1 = data1;
  usbMidiQueue[usbMidiHead].data2 = data2;
  usbMidiQueue[usbMidiHead].length = length;
  usbMidiHead = next;
  interrupts();
  return true;
}

bool usbMidiEnqueuePacket(const uint8_t packet[4])
{
  const uint8_t cin = packet[0] & 0x0F;
  const uint8_t len = usbMidiCinLength(cin);
  if(len == 0) return false;

  const uint8_t status = packet[1];
  if(status == 0) return false;
  return usbMidiEnqueue(cin, status, len > 1 ? packet[2] : 0, len > 2 ? packet[3] : 0, len);
}

bool usbMidiHandleDevicePacket(const uint8_t packet[4])
{
  const uint8_t cin = packet[0] & 0x0F;
  const bool mgbThruEnabled = memory[MEM_MODE] == 4;
  switch(cin) {
    case 0x4:
      checkForProgrammerSysex(packet[1]);
      checkForProgrammerSysex(packet[2]);
      checkForProgrammerSysex(packet[3]);
      return mgbThruEnabled ? usbMidiEnqueuePacket(packet) : true;
    case 0x5:
      checkForProgrammerSysex(packet[1]);
      return mgbThruEnabled ? usbMidiEnqueuePacket(packet) : true;
    case 0x6:
      checkForProgrammerSysex(packet[1]);
      checkForProgrammerSysex(packet[2]);
      return mgbThruEnabled ? usbMidiEnqueuePacket(packet) : true;
    case 0x7:
      checkForProgrammerSysex(packet[1]);
      checkForProgrammerSysex(packet[2]);
      checkForProgrammerSysex(packet[3]);
      return mgbThruEnabled ? usbMidiEnqueuePacket(packet) : true;
    default:
      return usbMidiEnqueuePacket(packet);
  }
}

void usbMidiPollDevice()
{
  uint8_t packet[4];
  while(usb_midi.readPacket(packet)) {
    usbMidiHandleDevicePacket(packet);
  }
}

bool usbMidiReadMessage(UsbMidiMessage *msg)
{
  usbMidiPollDevice();
  noInterrupts();
  if(usbMidiTail == usbMidiHead) {
    interrupts();
    return false;
  }

  *msg = usbMidiQueue[usbMidiTail];
  usbMidiTail = (usbMidiTail + 1) % USB_MIDI_QUEUE_SIZE;
  interrupts();
  return true;
}

uint8_t usbMidiCodeIndex(uint8_t status, uint8_t length)
{
  if(status >= 0xF8) return 0x0F;
  if(status == 0xF1 || status == 0xF3) return 0x02;
  if(status == 0xF2) return 0x03;
  if(status >= 0xF4) return 0x05;
  if((status & 0xF0) == 0xC0) return 0x0C;
  if((status & 0xF0) == 0xD0) return 0x0D;
  return (status & 0xF0) >> 4;
}

void usbMidiWriteRaw(uint8_t status, uint8_t data1, uint8_t data2, uint8_t length)
{
  uint8_t packet[4] = {usbMidiCodeIndex(status, length), status, data1, data2};
  usb_midi.writePacket(packet);
}

bool usbMidiHostTxEnqueue(const uint8_t packet[4])
{
  const uint8_t next = (usbMidiHostTxHead + 1) % USB_MIDI_HOST_TX_QUEUE_SIZE;
  if(next == usbMidiHostTxTail) return false;

  memcpy(usbMidiHostTxQueue[usbMidiHostTxHead], packet, 4);
  usbMidiHostTxHead = next;
  return true;
}

void usbMidiMgbThruToUsb(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2)
{
  // MGB mode turns the device and host connections into MIDI thru outputs.
  uint8_t packet[4] = {cin, status, data1, data2};
  usb_midi.writePacket(packet);
  usbMidiHostTxEnqueue(packet);
}

void usbMidiMgbThruToAll(const UsbMidiMessage *msg)
{
  const uint8_t bytes[3] = {msg->status, msg->data1, msg->data2};
  serial->write(bytes, msg->length);
  usbMidiMgbThruToUsb(msg->cin, msg->status, msg->data1, msg->data2);
}

void usbMidiSendTwoByteMessage(uint8_t b1, uint8_t b2)
{
  usbMidiWriteRaw(b1, b2, 0, 2);
}

void usbMidiSendThreeByteMessage(uint8_t b1, uint8_t b2, uint8_t b3)
{
  usbMidiWriteRaw(b1, b2, b3, 3);
}

void usbMidiSendRTMessage(uint8_t b)
{
  usbMidiWriteRaw(b, 0, 0, 1);
}

void usbMidiSendSysEx(const uint8_t *data, uint16_t length)
{
  uint16_t position = 0;
  while(length - position > 3) {
    uint8_t packet[4] = {0x04, data[position], data[position + 1], data[position + 2]};
    usb_midi.writePacket(packet);
    position += 3;
  }

  const uint8_t remaining = length - position;
  if(remaining == 1) {
    uint8_t packet[4] = {0x05, data[position], 0, 0};
    usb_midi.writePacket(packet);
  } else if(remaining == 2) {
    uint8_t packet[4] = {0x06, data[position], data[position + 1], 0};
    usb_midi.writePacket(packet);
  } else if(remaining == 3) {
    uint8_t packet[4] = {0x07, data[position], data[position + 1], data[position + 2]};
    usb_midi.writePacket(packet);
  }
}

void usbMidiHandleSysEx(const uint8_t *data, uint16_t length, bool complete)
{
  if(sysexPosition + length >= longestSysexMessage || (length < 3 && complete)) {
    sysexPosition = 0;
    return;
  }

  if(sysexPosition == 0 && complete) {
    memcpy(&sysexData[0], &data[1], length - 2);
    sysexPosition += length - 2;
  } else if(sysexPosition == 0 && !complete) {
    memcpy(&sysexData[0], &data[1], length - 1);
    sysexPosition += length - 1;
  } else if(!complete) {
    memcpy(&sysexData[sysexPosition], &data[0], length);
    sysexPosition += length;
  } else {
    memcpy(&sysexData[sysexPosition], &data[0], length - 1);
    sysexPosition += length - 1;
  }

  if(complete) getSysexData();
}

void usbMidiInit()
{
  if(!usbMidiDeviceStarted) {
    if(!TinyUSBDevice.isInitialized()) {
      TinyUSBDevice.begin(0);
    }
    TinyUSBDevice.setManufacturerDescriptor("Game Boy");
    TinyUSBDevice.setProductDescriptor("Game Boy");
    usb_midi.setStringDescriptor("Game Boy");
    usb_midi.begin();
    if(TinyUSBDevice.mounted()) {
      TinyUSBDevice.detach();
      delay(10);
      TinyUSBDevice.attach();
    }
    usbMidiDeviceStarted = true;
  }
}

void usbMidiStartHost()
{
  usbHostStartRequested = true;
}

void usbMidiUpdate()
{
  usbMidiPollDevice();
}

void setupUsbHost()
{
  if(clock_get_hz(clk_sys) != 120000000UL && clock_get_hz(clk_sys) != 240000000UL) return;

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = PIN_USB_HOST_DP;
  USBHost.configure_pio_usb(1, &pio_cfg);

  if(PIN_USB_HOST_VBUS_EN != 255) {
    pinMode(PIN_USB_HOST_VBUS_EN, OUTPUT);
    digitalWrite(PIN_USB_HOST_VBUS_EN, HIGH);
  }
}

void setup1()
{
  while(!usbHostStartRequested) delay(1);
  setupUsbHost();
  if(clock_get_hz(clk_sys) == 120000000UL || clock_get_hz(clk_sys) == 240000000UL) {
    USBHost.begin(1);
    // Pico-PIO-USB creates its SOF/transaction alarm pool on hardware alarm 2
    // (TIMER_IRQ_2), which drives the software-generated 1ms USB frame service this
    // whole host path depends on. Raising it to the highest IRQ priority means other
    // interrupts (DIN UART, USB device CDC/MIDI) can't delay that servicing. Cheap,
    // and arpnmidi's own testing found no downside to leaving it on.
    irq_set_priority(TIMER_IRQ_2, PICO_HIGHEST_IRQ_PRIORITY);
  }
}

void loop1()
{
  core1State = 2;
  // Adafruit_USBH_Host::task() defaults to timeout_ms=UINT32_MAX when called with
  // no argument, which lets the underlying tuh_task_ext() block indefinitely
  // waiting for the next USB event. Passing 0 makes it a non-blocking poll instead -
  // confirmed against the installed Adafruit TinyUSB Library 3.7.7 header, and
  // matches how the arpnmidi project's own RP2040-PIO-USB-host firmware calls it.
  USBHost.task(0);
  core1State = 1;

  uint8_t packet[4];
  uint32_t nowMs = millis();
  for(uint8_t i = 0; i < MAX_USB_MIDI_DEVICES; ++i) {
    if(!usbMidiDevices[i].mounted || usbMidiDevices[i].rxCableCount == 0) continue;
    bool readAny = false;
    while(tuh_midi_read_available(usbMidiDevices[i].idx) >= 4 && tuh_midi_packet_read(usbMidiDevices[i].idx, packet)) {
      usbMidiEnqueuePacket(packet);
      readAny = true;
    }
    if(readAny) {
      usbMidiDevices[i].lastRxMs = nowMs;
      continue;
    }

    // Nothing came in this pass. A mounted RX endpoint sits "busy" almost all the
    // time by design (TinyUSB immediately resubmits the pending read), so silence
    // alone can't tell a genuinely idle device apart from one whose transfer is
    // stuck and will never complete on its own. After USB_MIDI_RX_SILENCE_MS with
    // nothing received, force the endpoint to abort + resubmit instead of trusting
    // it'll recover by itself.
    const uint32_t referenceMs = usbMidiDevices[i].lastRxMs != 0 ? usbMidiDevices[i].lastRxMs : usbMidiDevices[i].mountedMs;
    if(nowMs - referenceMs < USB_MIDI_RX_SILENCE_MS) continue;
    if(usbMidiDevices[i].lastRearmMs != 0 && nowMs - usbMidiDevices[i].lastRearmMs < USB_MIDI_RX_SILENCE_MS) continue;

    usbMidiDevices[i].lastRearmMs = nowMs;
    if(tuh_midi_rx_rearm(usbMidiDevices[i].idx)) {
      Serial.print(F("[usb-host] RX rearmed on idx "));
      Serial.print(usbMidiDevices[i].idx);
      Serial.print(F(" after "));
      Serial.print(nowMs - referenceMs);
      Serial.println(F("ms silence"));
    }
  }

  while(usbMidiHostTxTail != usbMidiHostTxHead) {
    const uint8_t *txPacket = usbMidiHostTxQueue[usbMidiHostTxTail];
    for(uint8_t i = 0; i < MAX_USB_MIDI_DEVICES; ++i) {
      if(!usbMidiDevices[i].mounted || usbMidiDevices[i].txCableCount == 0) continue;
      if(tuh_midi_packet_write(usbMidiDevices[i].idx, txPacket)) {
        tuh_midi_write_flush(usbMidiDevices[i].idx);
      }
    }
    usbMidiHostTxTail = (usbMidiHostTxTail + 1) % USB_MIDI_HOST_TX_QUEUE_SIZE;
  }

  core1LoopCount++;
}

int8_t findUsbMidiDeviceSlot(uint8_t idx)
{
  for(uint8_t i = 0; i < MAX_USB_MIDI_DEVICES; ++i) {
    if(usbMidiDevices[i].mounted && usbMidiDevices[i].idx == idx) return i;
  }
  return -1;
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data)
{
  core1State = 3;
  if(idx >= MAX_USB_MIDI_DEVICES) return;
  for(uint8_t i = 0; i < MAX_USB_MIDI_DEVICES; ++i) {
    if(!usbMidiDevices[i].mounted) {
      usbMidiDevices[i].mounted = true;
      usbMidiDevices[i].idx = idx;
      usbMidiDevices[i].rxCableCount = mount_cb_data->rx_cable_count;
      usbMidiDevices[i].txCableCount = mount_cb_data->tx_cable_count;
      usbMidiDevices[i].mountedMs = millis();
      usbMidiDevices[i].lastRxMs = 0;
      usbMidiDevices[i].lastRearmMs = 0;
      break;
    }
  }
}

void tuh_midi_umount_cb(uint8_t idx)
{
  core1State = 4;
  if(idx >= MAX_USB_MIDI_DEVICES) return;
  for(uint8_t i = 0; i < MAX_USB_MIDI_DEVICES; ++i) {
    if(usbMidiDevices[i].mounted && usbMidiDevices[i].idx == idx) {
      usbMidiDevices[i] = UsbMidiDevice{};
      break;
    }
  }
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes)
{
  if(idx >= MAX_USB_MIDI_DEVICES || xferred_bytes == 0) return;

  const int8_t slot = findUsbMidiDeviceSlot(idx);
  if(slot >= 0) usbMidiDevices[slot].lastRxMs = millis();

  uint8_t packet[4];
  while(tuh_midi_packet_read(idx, packet)) {
    usbMidiEnqueuePacket(packet);
  }
}

void tuh_midi_tx_cb(uint8_t idx, uint32_t num_bytes)
{
  (void)idx;
  (void)num_bytes;
}

#elif !defined(USE_TEENSY)

void usbMidiSendTwoByteMessage(uint8_t b1, uint8_t b2) {};
void usbMidiSendThreeByteMessage(uint8_t b1, uint8_t b2, uint8_t b3) {};
void usbMidiSendRTMessage(uint8_t b) {};
void usbMidiHandleSysEx(const uint8_t *data, uint16_t length, bool complete) {};
void usbMidiSendSysEx(const uint8_t *data, uint16_t length) {};
void usbMidiInit() {};
void usbMidiStartHost() {};
void usbMidiUpdate() {};

#else

void usbMidiSendTwoByteMessage(uint8_t b1, uint8_t b2)
{
    uint8_t stat = b1 & 0xf0;
    uint8_t chan = (b1 & 0x0f)+1;
    if(stat == 0xC0) {
        usbMIDI.sendProgramChange(b2, chan);
    } else if (stat == 0xD0) {
        usbMIDI.sendAfterTouch(b2, chan);
    }
}

void usbMidiSendThreeByteMessage(uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint8_t channel = (b1&0x0F)+1;

    switch(midiData[0] & 0xF0) {
        case 0x80:
          usbMIDI.sendNoteOff(b2, b3, channel);
          usbMIDI.send_now();
          break;
        case 0x90:
          usbMIDI.sendNoteOn(b2, b3, channel);
          usbMIDI.send_now();
          break;
        case 0xA0:
          usbMIDI.sendPolyPressure(b2, b3, channel);
          break;
        case 0xB0:
          usbMIDI.sendControlChange(b2, b3, channel);
          usbMIDI.send_now();
          break;
        case 0xE0:
          unsigned short v = (unsigned short)b3;
          v<<=7;
          v|=(unsigned short)b2;
          usbMIDI.sendPitchBend(v, channel);
          break;
    }
}

void usbMidiSendRTMessage(uint8_t b)
{
    usbMIDI.sendRealTime(b);
}

void usbMidiUpdate()
{
    usbMIDI.read();
}

void usbMidiHandleSysEx(const uint8_t *data, uint16_t length, bool complete)
{
    if(sysexPosition + length >= longestSysexMessage || (length < 3 && complete)) {
        //wrapped!
        sysexPosition = 0;
        return ;
    }

    if(sysexPosition == 0 && complete) {
        memcpy(&sysexData[0], &data[1], length-2);
        sysexPosition += length-2;
    } else if (sysexPosition == 0 && !complete) {
        memcpy(&sysexData[0], &data[1], length-1);
        sysexPosition += length-1;
    } else if (!complete) {
        memcpy(&sysexData[sysexPosition], &data[0], length);
        sysexPosition += length;
    } else {
        memcpy(&sysexData[sysexPosition], &data[0], length-1);
        sysexPosition += length-1;
    }

    if(complete) {
        getSysexData();
    }
}

void usbMidiInit()
{
    usbMIDI.setHandleSysEx(usbMidiHandleSysEx);
}

void usbMidiStartHost() {};

#endif
