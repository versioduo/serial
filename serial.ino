// © Kay Sievers <kay@vrfy.org>, 2020-2021
// SPDX-License-Identifier: Apache-2.0

#include <V2Color.h>
#include <V2Device.h>
#include <V2LED.h>
#include <V2Link.h>
#include <V2MIDI.h>

V2DEVICE_METADATA("com.versioduo.serial", 5, "versioduo:samd:serial");

static V2LED::WS2812 LED(2, PIN_LED_WS2812, &sercom2, SPI_PAD_0_SCK_1, PIO_SERCOM);
static V2MIDI::SerialDevice MIDISerialDevice(&SerialMIDI);
static V2Link::Port Plug(&SerialPlug);
static V2Link::Port Socket(&SerialSocket);

static class Device : public V2Device {
public:
  Device() : V2Device() {
    metadata.vendor      = "Versio Duo";
    metadata.product     = "serial";
    metadata.description = "Serial MIDI Interface";
    metadata.home        = "https://versioduo.com/#serial";

    system.download  = "https://versioduo.com/download";
    system.configure = "https://versioduo.com/configure";

    configuration = {.magic{0x9e020000 | usb.pid}, .size{sizeof(config)}, .data{&config}};
  }

  // Config, written to EEPROM
  struct {
  } config{};

private:
  V2MIDI::Packet _midi{};

  void handleReset() override {
    digitalWrite(PIN_LED_ONBOARD, LOW);
    LED.reset();
  }

  void allNotesOff() {
    reset();
  }

  void handleLoop() override {
    if (!MIDISerialDevice.receive(&_midi))
      return;

    switch (_midi.getType()) {
      case V2MIDI::Packet::Status::NoteOn: {
        usb.midi.send(&_midi);
        Plug.send(&_midi);
        LED.setHSV(0, V2Color::Magenta, 1, (float)_midi.getNoteVelocity() / 127);
      } break;

      case V2MIDI::Packet::Status::NoteOff: {
        usb.midi.send(&_midi);
        Plug.send(&_midi);
        LED.setBrightness(0, 0);
      } break;

      case V2MIDI::Packet::Status::ControlChange: {
        usb.midi.send(&_midi);
        Plug.send(&_midi);
        LED.setHSV(0, V2Color::Cyan, 1, 1);
      } break;
    }
  }

  void handleNote(uint8_t channel, uint8_t note, uint8_t velocity) override {
    _midi.setNote(channel, note, velocity);
    MIDISerialDevice.send(&_midi);

    LED.setHSV(1, V2Color::Magenta, 1, (float)velocity / 127);
  }

  void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
    _midi.setNoteOff(channel, note, velocity);
    MIDISerialDevice.send(&_midi);

    LED.setBrightness(1, 0);
  }

  void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value) override {
    _midi.setControlChange(channel, controller, value);
    MIDISerialDevice.send(&_midi);

    switch (controller) {
      case V2MIDI::CC::AllSoundOff:
      case V2MIDI::CC::AllNotesOff:
        allNotesOff();
        break;
    }

    LED.setHSV(1, V2Color::Cyan, 1, (float)value / 127);
  }

  void handleSystemReset() override {
    reset();

    LED.splashHSV(0.3, 2, V2Color::Blue, 1, 1);
  }
} Device;

// Dispatch MIDI packets
static class MIDI {
public:
  void loop() {
    if (!Device.usb.midi.receive(&_midi))
      return;

    if (_midi.getPort() == 0) {
      Device.dispatch(&Device.usb.midi, &_midi);

    } else {
      _midi.setPort(_midi.getPort() - 1);
      Socket.send(&_midi);
    }
  }

private:
  V2MIDI::Packet _midi{};
} MIDI;

// Dispatch Link packets
static class Link : public V2Link {
public:
  Link() : V2Link(&Plug, &Socket) {}

private:
  V2MIDI::Packet _midi{};

  // Receive a host event from our parent device
  void receivePlug(V2Link::Packet *packet) override {
    if (packet->getType() == V2Link::Packet::Type::MIDI) {
      packet->receive(&_midi);
      Device.dispatch(&Plug, &_midi);
    }
  }

  // Forward children device events to the host
  void receiveSocket(V2Link::Packet *packet) override {
    if (packet->getType() == V2Link::Packet::Type::MIDI) {
      uint8_t address = packet->getAddress();
      if (address == 0x0f)
        return;

      if (Device.usb.midi.connected()) {
        packet->receive(&_midi);
        _midi.setPort(address + 1);
        Device.usb.midi.send(&_midi);
      }
    }
  }
} Link;

void setup() {
  Serial.begin(9600);

  LED.begin();
  LED.setMaxBrightness(0.5);
  Plug.begin();
  Socket.begin();

  // Set the SERCOM interrupt priority, it requires a stable ~300 kHz interrupt
  // frequency. This needs to be after begin().
  setSerialPriority(&SerialPlug, 2);
  setSerialPriority(&SerialSocket, 2);

  SerialMIDI.begin(31250);
  SerialMIDI.setTimeout(1);
  Device.begin();
  Device.reset();
}

void loop() {
  LED.loop();
  MIDI.loop();
  Link.loop();
  Device.loop();

  if (Link.idle() && Device.idle())
    Device.sleep();
}
