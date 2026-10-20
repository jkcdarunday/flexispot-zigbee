#pragma once
#include "desk_protocol.h"

namespace desk {
// Driver owns one UART and exposes a nonblocking ready()/write()/now() interface.
// Never build a backlog of keypresses behind a busy hardware FIFO.
template<class Driver> class DeskTransmitter {
 public:
  explicit DeskTransmitter(Driver &driver) : driver_(driver) {}
  bool send(Command requested) {
    if (requested == Command::Release && releaseCopies_ == 0) releaseCopies_ = 1;
    if (!driver_.ready()) {
      if (!waiting_) { waiting_ = true; busySince_ = driver_.now(); }
      ++busy_;
      return false;
    }
    waiting_ = false;
    const Command actual = releaseCopies_ ? Command::Release : requested;
    uint8_t packet[8];
    commandPacket(actual, packet);
    if (driver_.write(packet, sizeof(packet)) != sizeof(packet)) {
      ++errors_;
      if (consecutiveErrors_ < 3) ++consecutiveErrors_;
      // Two complete releases resynchronize the desk after a partial frame.
      releaseCopies_ = 2;
      return false;
    }
    consecutiveErrors_ = 0;
    if (actual == Command::Release && releaseCopies_) --releaseCopies_;
    lastSent_ = actual;
    return true;
  }
  bool faulted() {
    if (consecutiveErrors_ >= 3) return true;
    if (waiting_ && driver_.now() - busySince_ >= 1000) {
      if (!driver_.ready()) return true;
      waiting_ = false; // One busy attempt is not proof of a stuck transmitter.
    }
    return false;
  }
  bool releasePending() const { return releaseCopies_ != 0; }
  Command lastSent() const { return lastSent_; }
  uint32_t busyCount() const { return busy_; }
  uint32_t errorCount() const { return errors_; }
 private:
  Driver &driver_;
  uint8_t releaseCopies_ = 0;
  Command lastSent_ = Command::Release;
  uint32_t busy_ = 0, errors_ = 0, busySince_ = 0;
  bool waiting_ = false;
  uint8_t consecutiveErrors_ = 0;
};
}  // namespace desk
