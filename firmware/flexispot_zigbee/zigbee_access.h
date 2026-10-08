#pragma once
#include <Zigbee.h>

// UART deadlines and the reset button must not wait indefinitely for the stack.
inline bool acquireDeskZigbeeLock() {
  return esp_zb_lock_acquire(pdMS_TO_TICKS(50));
}
