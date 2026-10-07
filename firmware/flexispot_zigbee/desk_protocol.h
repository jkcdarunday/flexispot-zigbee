#pragma once
#include <stddef.h>
#include <stdint.h>

namespace desk {
// Protocol and seven-segment mapping adapted from dimitri-vs/flexispot-esphome.
enum class Command : uint8_t { Stand, Sit, Preset1, Preset2, Up, Down, Memory, Release };

inline uint16_t crc16(const uint8_t *data, size_t size) {
  uint16_t crc = 0xffff;
  while (size--) {
    crc ^= *data++;
    for (unsigned i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
  }
  return crc;
}

inline void commandPacket(Command command, uint8_t (&out)[8]) {
  static const uint8_t buttons[][2] = {
    {0x10, 0}, {0, 1}, {4, 0}, {8, 0}, {1, 0}, {2, 0}, {0x20, 0}, {0, 0}
  };
  out[0] = 0x9b; out[1] = 6; out[2] = 2;
  const unsigned index = static_cast<unsigned>(command);
  out[3] = buttons[index < 8 ? index : 7][0];
  out[4] = buttons[index < 8 ? index : 7][1];
  const uint16_t crc = crc16(out + 1, 4);
  out[5] = crc >> 8; out[6] = crc & 0xff; out[7] = 0x9d;
}

inline int digit(uint8_t raw) {
  static const uint8_t patterns[] = {0x3f, 6, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 7, 0x7f, 0x6f};
  const uint8_t segments = raw & 0x7f;
  if (!segments) return -2;  // blank
  for (int i = 0; i < 10; ++i) if (segments == patterns[i]) return i;
  return -1;  // error, dash, or unknown display
}

struct Event { bool poll = false; bool hasHeight = false; float height = 0; };

class Parser {
 public:
  bool feed(uint8_t byte, uint32_t now, Event &event) {
    event = {};
    if (used_ && now - lastByte_ > 100) used_ = 0;
    lastByte_ = now;
    if (!used_) {
      if (byte == 0x9b) buffer_[used_++] = byte;
      return false;
    }
    buffer_[used_++] = byte;
    if (used_ == 2) {
      if (byte < 4 || byte > sizeof(buffer_) - 2) {
        used_ = 0;
        if (byte == 0x9b) buffer_[used_++] = byte;
      }
      return false;
    }
    const size_t expected = buffer_[1] + 2;
    if (used_ < expected) return false;
    used_ = 0;
    const uint16_t received = (uint16_t(buffer_[expected - 3]) << 8) | buffer_[expected - 2];
    if (byte != 0x9d || crc16(buffer_ + 1, expected - 4) != received) {
      if (byte == 0x9b) buffer_[used_++] = byte;
      return false;
    }
    event.poll = buffer_[2] == 0x11;
    if (buffer_[2] == 0x12 && (buffer_[1] == 7 || buffer_[1] == 10)) {
      int a = digit(buffer_[3]), b = digit(buffer_[4]), c = digit(buffer_[5]);
      if (a == -2) a = 0;
      if (a >= 0 && b >= 0 && c >= 0) {
        event.height = float(100 * a + 10 * b + c) / ((buffer_[4] & 0x80) ? 10 : 1);
        event.hasHeight = event.height >= 20 && event.height <= 200;
      }
    }
    return true;
  }
 private:
  uint8_t buffer_[32] = {};
  size_t used_ = 0;
  uint32_t lastByte_ = 0;
};

class Controller {
 public:
  enum class State { Boot, Idle, WakeLow, WakeHigh, Active };
  Controller(uint32_t nudgeMs = 500, uint32_t presetMs = 1000)
      : nudgeMs_(nudgeMs), presetMs_(presetMs) {}
  void begin(uint32_t now) { state_ = State::Boot; since_ = now; pending_ = Command::Release; }
  bool request(Command command, uint32_t now) {
    if (command == Command::Release) { release(); return true; }
    // Drop requests during boot or an existing command; never extend a nudge via repeats.
    if (static_cast<unsigned>(command) >= 7 || state_ != State::Idle) return false;
    pending_ = command; state_ = State::WakeLow; since_ = now;
    return true;
  }
  void release() { pending_ = Command::Release; state_ = State::Idle; }
  void tick(uint32_t now) {
    const uint32_t elapsed = now - since_;
    if (state_ == State::Boot && elapsed >= 10000) state_ = State::Idle;
    else if (state_ == State::WakeLow && elapsed >= 100) { state_ = State::WakeHigh; since_ = now; }
    else if (state_ == State::WakeHigh && elapsed >= 1100) { state_ = State::Active; since_ = now; }
    else if (state_ == State::Active) {
      const bool nudge = pending_ == Command::Up || pending_ == Command::Down;
      if (elapsed >= (nudge ? nudgeMs_ : presetMs_)) release();
    }
  }
  bool wakeHigh() const { return state_ != State::WakeLow; }
  Command pollResponse() const { return state_ == State::Active ? pending_ : Command::Release; }
  State state() const { return state_; }
 private:
  State state_ = State::Boot;
  Command pending_ = Command::Release;
  uint32_t since_ = 0;
  uint32_t nudgeMs_, presetMs_;
};
}  // namespace desk
