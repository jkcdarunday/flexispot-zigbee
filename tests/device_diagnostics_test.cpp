#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/flexispot_zigbee/device_diagnostics.h"

static void acknowledge(desk::DiagnosticsSchedule &schedule) {
  for (unsigned i = 0; i < 3; ++i) schedule.reported(i);
}

int main() {
  assert(desk::uptimeSeconds(999999) == 0);
  assert(desk::uptimeSeconds(1000000) == 1);
  const uint64_t millisRollover = (uint64_t(UINT32_MAX) + 1) * 1000;
  assert(desk::uptimeSeconds(millisRollover) >= desk::uptimeSeconds(millisRollover - 1000));
  assert(desk::uptimeSeconds(millisRollover + 30000000) > 4294990);
  assert(desk::uptimeSeconds(365ULL * 86400 * 1000000) == 31536000);

  desk::DiagnosticsSchedule heartbeat;
  assert(heartbeat.reportsDue(0, false) == 0);
  assert(heartbeat.reportsDue(10, true) == 7);
  acknowledge(heartbeat);
  assert(heartbeat.reportsDue(30009, true) == 0);
  assert(heartbeat.reportsDue(30010, true) == 7);

  desk::DiagnosticsSchedule retry;
  assert(retry.reportsDue(0, true) == 7);
  retry.reported(0);
  retry.reported(2);
  // A busy Zigbee lock must not create a report storm or lose the failed field.
  for (unsigned i = 0; i < 100000; ++i) assert(retry.reportsDue(999, true) == 0);
  assert(retry.reportsDue(1000, true) == 2);
  retry.reported(1);
  assert(retry.reportsDue(2000, true) == 0);
  assert(retry.reportsDue(2001, false) == 0);
  // Rejoin immediately refreshes every field even before the retry interval.
  assert(retry.reportsDue(2002, true) == 7);

  desk::DiagnosticsSchedule wrapped;
  const uint32_t start = UINT32_MAX - 499;
  assert(wrapped.refreshDue(start));
  assert(!wrapped.refreshDue(start + 999));
  assert(wrapped.refreshDue(start + 1000));
  assert(wrapped.reportsDue(start, true) == 7);
  acknowledge(wrapped);
  assert(wrapped.reportsDue(start + 29999, true) == 0);
  assert(wrapped.reportsDue(start + 30000, true) == 7);
  puts("64-bit uptime, report cadence, bounded retries, reconnect, and rollover tests passed");
}
