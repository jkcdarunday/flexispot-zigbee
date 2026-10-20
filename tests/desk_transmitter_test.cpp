#include "../firmware/flexispot_zigbee/desk_transmitter.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
using namespace desk;
struct Driver {
  bool idle = true;
  uint32_t time = 0;
  uint32_t now() const { return time; }
  size_t limit = 8;
  std::vector<uint8_t> wire;
  bool ready() const { return idle; }
  size_t write(const uint8_t *data, size_t size) {
    size = std::min(size, limit);
    wire.insert(wire.end(), data, data + size);
    idle = false;
    return size;
  }
};
int main() {
  Driver driver;
  DeskTransmitter<Driver> tx(driver);
  assert(tx.send(Command::Up));
  for (unsigned i = 0; i < 100000; ++i) assert(!tx.send(Command::Up));
  assert(driver.wire.size() == 8); // No software backlog from poll floods.
  assert(!tx.send(Command::Release) && tx.releasePending());
  driver.idle = true;
  assert(tx.send(Command::Down));
  assert(tx.lastSent() == Command::Release); // Release wins over a new active key.
  assert(!tx.releasePending());
  driver.idle = true;
  assert(tx.send(Command::Down) && tx.lastSent() == Command::Down);

  for (size_t partial = 0; partial < 8; ++partial) {
    Driver broken;
    DeskTransmitter<Driver> retry(broken);
    broken.limit = partial;
    assert(!retry.send(Command::Up));
    assert(retry.releasePending() && retry.errorCount() == 1);
    broken.limit = 8;
    broken.idle = true; assert(retry.send(Command::Release));
    assert(retry.releasePending());
    broken.idle = true; assert(retry.send(Command::Release));
    assert(!retry.releasePending() && retry.lastSent() == Command::Release);
    Parser parser; Event event;
    unsigned validFrames = 0;
    for (auto byte : broken.wire) if (parser.feed(byte, 0, event)) ++validFrames;
    assert(validFrames >= 1); // Wire framing recovers after every partial length.
    uint8_t release[8]; commandPacket(Command::Release, release);
    assert(std::equal(release, release + 8, broken.wire.end() - 8));
  }
  Driver transient;
  transient.idle = false;
  DeskTransmitter<Driver> brief(transient);
  assert(!brief.send(Command::Up));
  transient.time = 1000; transient.idle = true;
  assert(!brief.faulted()); // Hardware drained even though no new send was attempted.

  Driver stalled;
  stalled.idle = false;
  stalled.time = UINT32_MAX - 999;
  DeskTransmitter<Driver> stuck(stalled);
  assert(!stuck.send(Command::Up));
  stalled.time = UINT32_MAX;
  assert(!stuck.faulted());
  stalled.time = 0;
  assert(stuck.faulted()); // TX timeout crosses millis() rollover.

  Driver faulty;
  faulty.limit = 0;
  DeskTransmitter<Driver> errors(faulty);
  for (unsigned i = 0; i < 3; ++i) {
    faulty.idle = true;
    assert(!errors.send(Command::Release));
  }
  assert(errors.faulted());
  faulty.idle = true; faulty.limit = 8;
  assert(errors.send(Command::Release));
  assert(!errors.faulted());
  std::cout << "UART backpressure, priority release, partial-write and stall recovery tests passed\n";
}
