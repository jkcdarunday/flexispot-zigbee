#pragma once

// GPIO numbers, not header positions. Override here or with -D build flags.
#ifndef DESK_TX_PIN
#define DESK_TX_PIN 11  // green, RJ45 pin 6
#endif
#ifndef DESK_RX_PIN
#define DESK_RX_PIN 12  // white-blue, RJ45 pin 5
#endif
#ifndef DESK_WAKE_PIN
#define DESK_WAKE_PIN 13  // blue, RJ45 pin 4
#endif
#ifndef RESET_BUTTON_PIN
#define RESET_BUTTON_PIN 9  // usual H2 Super Mini BOOT button; -1 disables it
#endif
#ifndef DESK_DISPLAY_IN_INCHES
#define DESK_DISPLAY_IN_INCHES 0  // must match the physical keypad display
#endif
#ifndef DESK_NUDGE_MS
#define DESK_NUDGE_MS 500  // bounded up/down press; increase deliberately if needed
#endif
#ifndef DESK_PRESET_HOLD_MS
#define DESK_PRESET_HOLD_MS 1000
#endif
#ifndef DESK_DEBUG_UART
#define DESK_DEBUG_UART 0  // raw RX/TX hex on USB serial
#endif

static_assert(DESK_TX_PIN != DESK_RX_PIN && DESK_TX_PIN != DESK_WAKE_PIN &&
              DESK_RX_PIN != DESK_WAKE_PIN, "Desk GPIOs must be distinct");
static_assert(RESET_BUTTON_PIN < 0 ||
              (RESET_BUTTON_PIN != DESK_TX_PIN && RESET_BUTTON_PIN != DESK_RX_PIN &&
               RESET_BUTTON_PIN != DESK_WAKE_PIN), "Reset GPIO overlaps desk wiring");
static_assert(DESK_NUDGE_MS > 0 && DESK_NUDGE_MS <= 5000, "Nudge must be 1..5000 ms");
static_assert(DESK_PRESET_HOLD_MS > 0 && DESK_PRESET_HOLD_MS <= 1500,
              "Preset hold must be 1..1500 ms");
