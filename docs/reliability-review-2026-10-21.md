# Reliability review — 2026-10-21

Reviewed the firmware, UART parser and command state machine, LED timing, native
ZHA handler, Zigbee2MQTT converter, tests, CI, and the Arduino-ESP32 3.3.6 Zigbee
and native USB implementations. The reported failure has not been reproduced on
hardware during this review. These are identified defects and hardening changes,
not a confirmed diagnosis of the original multi-hour outage.

## Findings and changes

| Finding | Impact | Change |
| --- | --- | --- |
| Arduino's receiver-on helper only stores a flag, used by its battery-device workaround. The application never explicitly applied or verified SDK receiver/sleep state. | Always-awake operation was assumed rather than enforced. This does not establish that the old SDK defaults were actually sleeping. | Set SDK RX-on-when-idle true and Zigbee sleep false under the Zigbee lock, verify readback, and repeat each minute. CPU PM is disabled in the pinned SDK; a no-light-sleep lock covers builds enabling it. |
| Loop watchdog was enabled after `Zigbee.begin()`. | A stuck startup call was outside the application's watchdog coverage. | Enable a 60-second watchdog before startup; feed it before switching to the 30-second runtime limit. |
| Recovery only armed after pairing/connection or `Zigbee.started()`. | Initialization could keep retrying forever without ever arming recovery. | Watch initialization from before `begin()`, with a five-minute deadline. A running unpaired stack remains exempt from connection recovery. |
| Command draining used an unbounded `while`, despite a bounded queue. | A concurrent producer could keep filling the queue and starve UART/deadline processing. Queued commands had no age limit. | Drain at most eight per iteration. Timestamp queued commands and discard requests older than one second; retain priority Release behavior. |
| Every SDK lock acquisition could wait 50 ms. | Multiple operations in an iteration could accumulate latency while a key was active. | Attempt locks without waiting; existing bounded retry schedules retain pending reports. The watchdog remains necessary for stalls inside SDK calls. |
| Green LED relied only on Arduino's cached connection flag. | It could stay green after the coordinator became unreachable. | Require a successful coordinator reply within the last two minutes as well. |
| Sparse logs did not distinguish power resets, watchdogs, heap trends, or UART loss. | Long outages were difficult to diagnose after disconnecting USB. | Repeat reset reason, uptime, minimum/current heap, coordinator and UART counters in a minute status line. Bound application logging and avoid formatting allocations. Logging is best effort, not persistent crash storage. |

## Checks without additional changes

- Protocol parser uses a fixed 32-byte buffer, validates lengths and CRC, and
  bounds serial processing. No per-frame allocation was found.
- Application endpoints and request queue are created once. No application
  allocation growing with uptime was found; this does not audit allocations
  inside Espressif's binary Zigbee stack.
- SDK attribute registration follows the same local-value pattern used by the
  pinned Arduino Analog endpoint implementation. No lifetime defect established.
- Existing callbacks only enqueue commands or update flags under a short
  critical section; they do not perform desk I/O or print logs.
- Parent keepalive and end-device aging remain at the library defaults. These
  are network liveness mechanisms, not evidence of CPU sleep. Arbitrarily
  extending aging time could hide a lost connection, so it was not done.
- Reports still follow configured bindings. A queued report or coordinator
  descriptor response does not prove Home Assistant received a height report.
- ZHA descriptors, endpoints, button metadata, configurable pins, and the
  two-second Up/Down duration remain unchanged.

## Validation

- ASan/UBSan protocol, state-machine, LED and connection-health tests.
- Added simulated startup failure, running-unpaired behavior, lost coordinator
  replies, timer rollover, 72 hours of healthy reply timing, stale queued
  commands, and the configured two-second press deadline.
- ESP32-H2 firmware compile using the pinned Arduino-ESP32 3.3.6 core.
- CI also runs native ZHA discovery/button and real Zigbee2MQTT converter tests.

A simulated 72-hour timer test is not a 72-hour RF or hardware soak test. No
firmware was flashed during this review, at the user's request. Before claiming
the outage resolved, flash the reviewed build, verify `rx_idle=1 sleep=0` and
advancing coordinator replies, then run beyond the previous failure interval
on the actual desk power/wiring. Capture a status log if it fails. Unstable
power, intermittent wiring, RF conditions and SDK defects remain possible.

## Second pass before flashing

Additional findings and fixes:

| Finding | Impact | Change |
| --- | --- | --- |
| `rgbLedWriteOrdered()` calls `rmtWrite(..., RMT_WAIT_FOR_EVER)` in the pinned core. | A missing transmission completion could stop the entire main loop. The first LED update also preceded watchdog registration. | Use one asynchronous RMT transfer with a persistent, non-copyable payload owner. Disable the cosmetic LED after a missing completion, without freeing or reusing an in-flight payload. Arm the startup watchdog before all application peripheral initialization. |
| `HardwareSerial::write()` always returns the requested length, while its lower layer calls blocking `uart_write_bytes()`. | The previous apparent success check could not detect driver errors; a poll flood could hold the loop in transmit waits or build a stale TX FIFO backlog. | Use `uart_wait_tx_done(..., 0)` and `uart_tx_chars()` with software TX buffering disabled. Keep only one frame in flight. Preserve Release while busy, prioritize it over new active keys, cancel the active command on a short write and send two Release frames to resynchronize framing. Detect a one-second TX stall or three consecutive failed writes and recover with saved pairing. |
| Controller expiration was checked once per outer loop. | A delayed/buffered poll batch could still obtain an active command after its duration elapsed. Wake GPIO writes could also be delayed by logging after the timer had already started. | Check the timer with fresh time for every poll and immediately update wake GPIO. Apply wake transitions/request GPIO changes before command logs. |
| `release()` unconditionally changed Boot to Idle. | A Release during startup bypassed the documented ten-second movement guard. | Preserve Boot when releasing a key; release remains available during startup. |
| A wake sequence could resume after a multi-second processing stall. | The bridge could start a movement long after accepting the original request, even though the command queue itself had an age limit. | Abandon wake sequences delayed beyond the nominal 1.2-second sequence plus a one-second grace interval. |

Validation adds mocked RMT completion loss, persistent-buffer checks, all six
color orders, brightness and timer rollover, 100,000 busy UART polls, priority
Release, every partial UART write length from zero to seven bytes with wire
resynchronization, stuck/busy versus drained UART distinctions, TX fault timing
across clock rollover, consecutive failure recovery, buffered-poll expiration, stalled wake abandonment, and
startup-guard preservation. Hardware mocks validate failure handling and do not
simulate RF, electrical behavior, or actual interrupt timing. The H2 firmware
compile verifies the real SDK interfaces.

Upstream implementations inspected:
- [Arduino RGB LED helper](https://github.com/espressif/arduino-esp32/blob/3.3.6/cores/esp32/esp32-hal-rgb-led.c)
- [Arduino RMT driver](https://github.com/espressif/arduino-esp32/blob/3.3.6/cores/esp32/esp32-hal-rmt.c)
- [Arduino HardwareSerial](https://github.com/espressif/arduino-esp32/blob/3.3.6/cores/esp32/HardwareSerial.cpp)
- [ESP-IDF 5.5.2 UART implementation](https://github.com/espressif/esp-idf/blob/v5.5.2/components/esp_driver_uart/src/uart.c) and the pinned UART header document `uart_tx_chars()` as nonblocking and
  requiring disabled software TX buffering. This application has one UART TX
  owner; callbacks never write to the desk. SDK-internal mutex failures still
  require watchdog recovery.

The UART timing/transport and LED output changes need bench verification on the
actual desk before a long soak. The two-second Up/Down setting and Zigbee
descriptors remain unchanged. No flashing was performed in this pass.
