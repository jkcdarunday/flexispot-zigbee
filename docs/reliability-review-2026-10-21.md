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
