#pragma once
#include <stdint.h>

namespace desk {
struct LedColor {
  uint8_t red, green, blue;
  bool operator==(const LedColor &other) const {
    return red == other.red && green == other.green && blue == other.blue;
  }
};

// Pure timer logic, independent of Arduino. No waits or GPIO operations.
class StatusIndicator {
 public:
  void commandSent(uint32_t now) { commandAt_ = now; commandPulse_ = true; }
  void heightChanged(uint32_t now) {
    // Do not stretch a flash into a solid light during a stream of height frames.
    if (!heightPulse_ || now - heightAt_ >= 300) {
      heightAt_ = now; heightPulse_ = true;
    }
  }
  LedColor color(uint32_t now, bool connected, bool fault = false) {
    if (commandPulse_ && now - commandAt_ >= 600) commandPulse_ = false;
    if (heightPulse_ && now - heightAt_ >= 300) heightPulse_ = false;
    if (fault) return {255, 0, 0};
    const LedColor baseline = connected ? LedColor{0, 64, 0} :
        ((now / 500) % 2 == 0 ? LedColor{128, 64, 0} : LedColor{0, 0, 0});
    // Three purple flashes at the start of an actual UART command. Command
    // indication wins over height changes, but height events keep their timers.
    if (commandPulse_ && now - commandAt_ < 600)
      return ((now - commandAt_) / 100) % 2 == 0 ? LedColor{255, 0, 255} : baseline;
    if (heightPulse_ && now - heightAt_ < 150) return {0, 255, 255};
    return baseline;
  }
 private:
  bool commandPulse_ = false, heightPulse_ = false;
  uint32_t commandAt_ = 0, heightAt_ = 0;
};
}  // namespace desk
