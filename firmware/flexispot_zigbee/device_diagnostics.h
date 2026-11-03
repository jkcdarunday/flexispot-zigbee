#pragma once
#include <stdint.h>

namespace desk {
// esp_timer_get_time() is 64-bit; do not derive uptime from wrapping millis().
inline float uptimeSeconds(uint64_t microseconds) {
  return static_cast<float>(microseconds / 1000000ULL);
}

class DiagnosticsSchedule {
 public:
  bool refreshDue(uint32_t now) {
    if (refreshed_ && now - lastRefresh_ < 1000) return false;
    refreshed_ = true;
    lastRefresh_ = now;
    return true;
  }
  uint8_t reportsDue(uint32_t now, bool connected) {
    if (!connected) { wasConnected_ = false; return 0; }
    const bool reconnected = !wasConnected_;
    wasConnected_ = true;
    if (reconnected || now - lastScheduled_ >= 30000) {
      pending_ |= 0x07;
      lastScheduled_ = now;
    }
    if (!pending_ || (!reconnected && attempted_ && now - lastAttempt_ < 1000)) return 0;
    attempted_ = true;
    lastAttempt_ = now;
    return pending_;
  }
  void reported(unsigned index) { pending_ &= ~(1u << index); }
 private:
  uint32_t lastRefresh_ = 0, lastScheduled_ = 0, lastAttempt_ = 0;
  uint8_t pending_ = 0;
  bool refreshed_ = false, attempted_ = false, wasConnected_ = false;
};
}  // namespace desk
