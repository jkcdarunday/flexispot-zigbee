#pragma once
#include <Zigbee.h>

// Never stall UART deadlines behind the stack. Callers retain work and retry.
inline bool acquireDeskZigbeeLock() {
  return esp_zb_lock_acquire(0);
}
