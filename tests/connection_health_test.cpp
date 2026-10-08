#include <cassert>
#include <cstdint>
#include <iostream>
#include "../firmware/flexispot_zigbee/connection_health.h"

int main() {
  desk::ConnectionHealth unpaired;
  unpaired.observe(0, false);
  unpaired.observe(3600000, false);
  assert(!unpaired.needsRecovery(3600000));

  desk::ConnectionHealth rejoin;
  rejoin.paired(30000);
  rejoin.observe(30000, false);
  assert(!rejoin.needsRecovery(329999));
  assert(rejoin.needsRecovery(330000));

  desk::ConnectionHealth silent;
  silent.observe(1000, true);
  assert(!silent.needsRecovery(300999));
  // connected() can remain true even when packets never reach the coordinator.
  silent.observe(301000, true);
  assert(silent.needsRecovery(301000));

  desk::ConnectionHealth healthy;
  healthy.observe(1000, true);
  for (uint32_t now = 61000; now < 24 * 3600000; now += 60000) {
    healthy.observe(now, true);
    healthy.reply(now);
    assert(!healthy.needsRecovery(now));
  }

  desk::ConnectionHealth offline;
  offline.observe(1000, true);
  offline.reply(90000);
  offline.observe(100000, false);
  assert(!offline.needsRecovery(389999));
  assert(offline.needsRecovery(390000));
  offline.observe(390000, true);
  offline.reply(390000);
  assert(!offline.needsRecovery(390000));

  desk::ConnectionHealth wrap;
  const uint32_t start = UINT32_MAX - 100000;
  wrap.observe(start, true);
  assert(!wrap.needsRecovery(start + 299999u));
  assert(wrap.needsRecovery(start + 300000u));
  wrap.reply(start + 300000u);
  assert(!wrap.needsRecovery(start + 300001u));
  std::cout << "Connection health tests passed\n";
}
