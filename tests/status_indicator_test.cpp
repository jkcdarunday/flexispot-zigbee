#include "../firmware/flexispot_zigbee/status_indicator.h"
#include <assert.h>
#include <stdio.h>

using namespace desk;
int main() {
  StatusIndicator led;
  const LedColor green{0,64,0}, amber{128,64,0}, off{0,0,0};
  const LedColor purple{255,0,255}, cyan{0,255,255}, red{255,0,0};
  assert(led.color(0, true) == green);
  assert(led.color(0, false) == amber);
  assert(led.color(500, false) == off);
  assert(led.color(1000, false) == amber);
  led.heightChanged(1000);
  assert(led.color(1000, true) == cyan);
  led.heightChanged(1100);  // frequent frames do not prolong the flash
  assert(led.color(1150, true) == green);
  assert(led.color(1200, true) == green);
  led.heightChanged(1300);
  assert(led.color(1300, true) == cyan);
  led.commandSent(1300);
  assert(led.color(1300, true) == purple);
  assert(led.color(1400, true) == green);
  assert(led.color(1500, true) == purple);
  assert(led.color(1600, true) == green);
  assert(led.color(1700, true) == purple);
  assert(led.color(1800, true) == green);
  led.heightChanged(1850);
  assert(led.color(1900, true) == cyan);  // resume recent height event after command
  assert(led.color(2000, true) == green);
  assert(led.color(2000, true, true) == red);
  led.commandSent(UINT32_MAX - 49);
  assert(led.color(0, true) == purple);
  assert(led.color(50, true) == green);
  assert(led.color(550, true) == green);  // expiry across timer rollover
  puts("LED connection, event priority, repeat-frame, expiry, and rollover tests passed.");
}
