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
constexpr uint8_t MAX_USB_MIDI_DEVICES = 8;

Adafruit_USBD_MIDI usb_midi;
Adafruit_USBH_Host USBHost;

volatile bool usbHostStartRequested = false;
volatile uint8_t usbMidiHead = 0;
volatile uint8_t usbMidiTail = 0;
UsbMidiMessage usbMidiQueue[USB_MIDI_QUEUE_SIZE];
bool usbMidiDeviceStarted = false;

struct UsbMidiDevice {
  bool mounted;
  uint8_t idx;
  uint8_t rxCableCount;
};

UsbMidiDevice usbMidiDevices[MAX_USB_MIDI_DEVICES];

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

bool usbMidiEnqueue(uint8_t status, uint8_t data1, uint8_t data2, uint8_t length)
{
  noInterrupts();
  const uint8_t next = (usbMidiHead + 1) % USB_MIDI_QUEUE_SIZE;
  if(next == usbMidiTail) {
    interrupts();
    return false;
  }

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
  const uint8_t len = usbMidiCinLength(packet[0] & 0x0F);
  if(len == 0) return false;

  const uint8_t status = packet[1];
  if(status == 0) return false;
  return usbMidiEnqueue(status, len > 1 ? packet[2] : 0, len > 2 ? packet[3] : 0, len);
}

bool usbMidiHandleDevicePacket(const uint8_t packet[4])
{
  const uint8_t cin = packet[0] & 0x0F;
  switch(cin) {
    case 0x4:
      checkForProgrammerSysex(packet[1]);
      checkForProgrammerSysex(packet[2]);
      checkForProgrammerSysex(packet[3]);
      return true;
    case 0x5:
      checkForProgrammerSysex(packet[1]);
      return true;
    case 0x6:
      checkForProgrammerSysex(packet[1]);
      checkForProgrammerSysex(packet[2]);
      return true;
    case 0x7:
      checkForProgrammerSysex(packet[1]);
      checkForProgrammerSysex(packet[2]);
      checkForProgrammerSysex(packet[3]);
      return true;
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
  if((status & 0xF0) == 0xC0) return 0x0C;
  if((status & 0xF0) == 0xD0) return 0x0D;
  return (status & 0xF0) >> 4;
}

void usbMidiWriteRaw(uint8_t status, uint8_t data1, uint8_t data2, uint8_t length)
{
  uint8_t packet[4] = {usbMidiCodeIndex(status, length), status, data1, data2};
  usb_midi.writePacket(packet);
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
    TinyUSBDevice.setManufacturerDescriptor("Gameboy");
    TinyUSBDevice.setProductDescriptor("Gameboy");
    usb_midi.setStringDescriptor("Gameboy");
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
  }
}

void loop1()
{
  USBHost.task();

  uint8_t packet[4];
  for(uint8_t i = 0; i < MAX_USB_MIDI_DEVICES; ++i) {
    if(!usbMidiDevices[i].mounted || usbMidiDevices[i].rxCableCount == 0) continue;
    while(tuh_midi_read_available(usbMidiDevices[i].idx) >= 4 && tuh_midi_packet_read(usbMidiDevices[i].idx, packet)) {
      usbMidiEnqueuePacket(packet);
    }
  }
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data)
{
  if(idx >= MAX_USB_MIDI_DEVICES) return;
  for(uint8_t i = 0; i < MAX_USB_MIDI_DEVICES; ++i) {
    if(!usbMidiDevices[i].mounted) {
      usbMidiDevices[i].mounted = true;
      usbMidiDevices[i].idx = idx;
      usbMidiDevices[i].rxCableCount = mount_cb_data->rx_cable_count;
      break;
    }
  }
}

void tuh_midi_umount_cb(uint8_t idx)
{
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
