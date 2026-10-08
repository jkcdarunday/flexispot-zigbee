#pragma once
#include <stdint.h>

namespace desk {
// Pure timing policy; only a successful coordinator reply proves reachability.
// A queued report and Arduino's cached connected() flag do not prove delivery.
class ConnectionHealth {
 public:
  explicit ConnectionHealth(uint32_t timeoutMs = 300000) : timeoutMs_(timeoutMs) {}
  void paired(uint32_t now) {
    if (!everConnected_) {
      everConnected_ = true;
      lastReply_ = disconnectedSince_ = now;
    }
  }
  void observe(uint32_t now, bool connected) {
    if (connected) paired(now);
    if (!connected && wasConnected_) disconnectedSince_ = now;
    wasConnected_ = connected;
  }
  void reply(uint32_t now) { lastReply_ = now; }
  bool needsRecovery(uint32_t now) const {
    return everConnected_ &&
        (now - lastReply_ >= timeoutMs_ ||
         (!wasConnected_ && now - disconnectedSince_ >= timeoutMs_));
  }
 private:
  const uint32_t timeoutMs_;
  bool everConnected_ = false, wasConnected_ = false;
  uint32_t lastReply_ = 0, disconnectedSince_ = 0;
};
}  // namespace desk
