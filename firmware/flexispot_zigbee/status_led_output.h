#pragma once
#include <esp32-hal-rgb-led.h>
#include <esp32-hal-rmt.h>
#include "status_indicator.h"

namespace desk {
// The RMT encoder may retain the payload until its completion interrupt.
// Keep it in this long-lived object and never rewrite it while TX is pending.
class StatusLedOutput {
 public:
  StatusLedOutput(int pin, rgb_led_color_order_t order, uint8_t brightness)
      : pin_(pin), order_(order), brightness_(brightness) {}
  StatusLedOutput(const StatusLedOutput &) = delete;
  StatusLedOutput &operator=(const StatusLedOutput &) = delete;
  void begin() {
    if (begun_) return;
    begun_ = true;
    if (pin_ < 0) return;
    enabled_ = rmtInit(pin_, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
    failed_ = !enabled_;
  }
  void show(LedColor color, uint32_t now) {
    if (!enabled_) return;
    if (pending_) {
      if (!rmtTransmitCompleted(pin_)) {
        // Cosmetic failures must not wedge the UART/connection service.
        if (now - sentAt_ >= 50) { failed_ = true; enabled_ = false; }
        return;
      }
      pending_ = false;
    }
    if (hasColor_ && color == previous_) return;
    const uint8_t r = uint16_t(color.red) * brightness_ / 255;
    const uint8_t g = uint16_t(color.green) * brightness_ / 255;
    const uint8_t b = uint16_t(color.blue) * brightness_ / 255;
    uint8_t channels[3] = {g, r, b};
    switch (order_) {
      case LED_COLOR_ORDER_RGB: channels[0]=r; channels[1]=g; channels[2]=b; break;
      case LED_COLOR_ORDER_BGR: channels[0]=b; channels[1]=g; channels[2]=r; break;
      case LED_COLOR_ORDER_BRG: channels[0]=b; channels[1]=r; channels[2]=g; break;
      case LED_COLOR_ORDER_RBG: channels[0]=r; channels[1]=b; channels[2]=g; break;
      case LED_COLOR_ORDER_GBR: channels[0]=g; channels[1]=b; channels[2]=r; break;
      default: break;
    }
    for (unsigned i = 0; i < 24; ++i) {
      const bool one = channels[i / 8] & (0x80 >> (i % 8));
      pulses_[i].level0 = 1;
      pulses_[i].duration0 = one ? 8 : 4;
      pulses_[i].level1 = 0;
      pulses_[i].duration1 = one ? 4 : 8;
    }
    // End with a 400 us LOW reset/latch interval at 10 MHz.
    pulses_[24].level0 = pulses_[24].level1 = 0;
    pulses_[24].duration0 = pulses_[24].duration1 = 2000;
    if (!rmtWriteAsync(pin_, pulses_, 25)) {
      failed_ = true;
      enabled_ = false;
      return;
    }
    previous_ = color;
    hasColor_ = pending_ = true;
    sentAt_ = now;
  }
  bool failed() const { return failed_; }
 private:
  const int pin_;
  const rgb_led_color_order_t order_;
  const uint8_t brightness_;
  bool begun_ = false, enabled_ = false, failed_ = false;
  bool pending_ = false, hasColor_ = false;
  uint32_t sentAt_ = 0;
  LedColor previous_{0, 0, 0};
  rmt_data_t pulses_[25] = {};
};
}  // namespace desk
