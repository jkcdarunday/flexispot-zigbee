# Flexispot E7Q Zigbee bridge

Firmware for a **Super Mini ESP32-H2**, adapting the desk protocol from
[dimitri-vs/flexispot-esphome](https://github.com/dimitri-vs/flexispot-esphome)
to native Zigbee. Pair it with a Home Assistant **ZHA** or **Zigbee2MQTT**
coordinator. The default build is an always-awake **Zigbee router**: it controls
the desk and relays mesh traffic through neighboring routers. No Wi-Fi, ESPHome
server, or MQTT client runs on the H2.

**Validation:** compiled for ESP32-H2 with Arduino-ESP32 3.3.6 and tested on
the host for packet encoding, serial corruption, wake timing, bounded presses,
and timer rollover. Actual E7Q movement and radio pairing still require testing
on your desk. Upstream tested an E7 Pro Plus / HS13M-1C0 / CB38M2L; E7Q support
is expected from the shared LoctekMotion protocol, not confirmed by that test.

## Wiring

Defaults match your wiring. These are **GPIO numbers**, not header positions.
Colors assume a T568B cable; verify connector orientation and voltages before
connecting. Use the control box's spare keypad port and leave the physical
keypad connected.

| Cable wire | RJ45 pin | H2 connection | Function |
| --- | --- | --- | --- |
| Brown | 8 | 5V/VBUS input | Desk power |
| White-brown | 7 | GND | Common ground |
| Green | 6 | GPIO10 | UART TX: H2 → desk |
| Light blue / white-blue | 5 | GPIO11 | UART RX: desk → H2 |
| Blue | 4 | GPIO12 | Wake / keypad enable |

The other three wires are unused. This is a proprietary UART connector,
**not Ethernet**; never connect it to network equipment.

ESP32-H2 GPIOs are **3.3V**, not 5V tolerant. The brown wire belongs only on
the board's regulated 5V power input, never a GPIO or 3V3 pin. Measure your
desk's signal levels; if they exceed 3.3V, use appropriate level translation
on RX, TX, and wake. The upstream project shows a three-channel level-shifter
connection. Do not assume the exact E7Q controller matches its measured levels.
For first tests power the H2 by USB with **brown disconnected**. Avoid connecting
USB power and desk power simultaneously unless your board's power circuit
explicitly supports it; Super Mini boards vary.

## Configure

Edit [`firmware/flexispot_zigbee/config.h`](firmware/flexispot_zigbee/config.h):

```cpp
#define DESK_TX_PIN 10
#define DESK_RX_PIN 11
#define DESK_WAKE_PIN 12
#define RESET_BUTTON_PIN 9
#define DESK_DISPLAY_IN_INCHES 0
#define DESK_NUDGE_MS 2000
#define DESK_PRESET_HOLD_MS 1000
#define DESK_DEBUG_UART 0
#define STATUS_LED_PIN 8
#define STATUS_LED_BRIGHTNESS 24
```

The file already contains these defaults guarded by `#ifndef`. Change the
values there or override them with compiler `-D` flags. Pins are validated for
duplicates and valid H2 GPIOs. Avoid flash, USB (GPIO26/27), and other
board-reserved pins when changing them; consult your exact board schematic.
GPIO9 is the usual BOOT button; change it for a different board or use `-1`
to disable button reset. Holding BOOT during power-on may instead enter the
bootloader; firmware reset is a five-second hold **after startup**.

`DESK_DISPLAY_IN_INCHES` must match the keypad's unit setting. Heights are always
converted to **cm over Zigbee**. Firmware does not change the desk's unit setting.
The initial height is unknown until a valid display frame arrives; no fabricated
height or automatic memory press is sent at boot.

## Build and flash

### Arduino IDE

1. Install **esp32 by Espressif Systems, version 3.3.6** in Boards Manager.
   If needed, use this additional board URL:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
2. Open `firmware/flexispot_zigbee/flexispot_zigbee.ino`.
3. Select **ESP32H2 Dev Module** and your USB port.
4. Set **Zigbee mode → Zigbee ZCZR (coordinator/router)**,
   **Partition Scheme → Zigbee ZCZR 4MB with spiffs**, **Flash Size → 4MB**,
   and **USB CDC On Boot → Enabled**. Verify your board actually has 4MB flash.
5. Upload. If it does not enter the bootloader automatically, hold BOOT, tap
   RESET, release BOOT, then upload. Reboot afterward.

### Arduino CLI

```sh
arduino-cli core install esp32:esp32@3.3.6 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
bash scripts/build.sh
arduino-cli upload \
  --fqbn 'esp32:esp32:esp32h2:ZigbeeMode=zczr,PartitionScheme=zigbee_zczr,CDCOnBoot=cdc,FlashSize=4M' \
  --port /dev/ttyACM0 --input-dir build firmware/flexispot_zigbee
```

Replace the port with your board's port. `ARDUINO_CLI` can override the executable.
For example, compile with different GPIOs without editing defaults:

```sh
bash scripts/build.sh --build-property \
  'compiler.cpp.extra_flags=-DDESK_TX_PIN=10 -DDESK_RX_PIN=11 -DDESK_WAKE_PIN=12'
```

The script defaults to router mode. To build the previous always-awake end-device
role, use `ZIGBEE_ROLE=end-device bash scripts/build.sh --output-dir build/end-device`.
For that build, select `ZigbeeMode=ed,PartitionScheme=zigbee` when uploading;
in the IDE select **Zigbee ED (end device)** and **Zigbee 4MB with spiffs**.
The firmware selects its role explicitly from the selected Zigbee mode; ZCZR
always starts a router, never a coordinator.

For a **2MB** H2 router board use
`FlashSize=2M,PartitionScheme=zigbee_zczr_2MB` instead of the 4MB settings
(both when compiling and uploading). For a 2MB end-device build, use
`FlashSize=2M,PartitionScheme=zigbee_2MB`. Pass the full modified FQBN with
`--fqbn` to `scripts/build.sh` when compiling. Do not flash a 4MB partition
layout to a 2MB board. CI compiles both roles and publishes separate artifacts.

### Switching an existing installation to router mode

This is a one-time network-role migration. Arduino-ESP32 3.3.6 uses different
`zb_storage` and `zb_fct` offsets for ED and ZCZR builds. **An application-only
flash at `0x10000` is insufficient.** Upload the router partition table and
firmware together using the Arduino IDE or the CLI upload command above.

1. Record the desk's Home Assistant entity IDs and automations, then remove
   the existing desk device from ZHA so the router role gets a fresh interview.
2. For this first router upload, enable **Erase All Flash Before Sketch Upload**
   in the IDE. With the CLI, first run
   `esptool --chip esp32h2 --port YOUR_PORT erase-flash`, then the upload command.
   This clears the old pairing and both overlapping Zigbee storage layouts.
   It also erases any other data on this ESP32; use it only for the migration.
3. Enable adding devices in ZHA, then boot the router firmware and pair the desk.
   Reinstalling the optional ZHA handler is unnecessary. Check entity IDs and
   adjust automations if Home Assistant assigned different ones.
4. Confirm ZHA identifies the device as a **Router**, the LED settles to green,
   and new commands and height updates work. Multiple map links depend on
   neighboring routers and the information available to ZHA; they are not guaranteed.

Normal updates within the same role preserve pairing; keep erase-all disabled
for those. Reverting to ED requires the same erase, matching partition upload,
and pairing procedure. Router operation requires reliable continuous power and
an antenna clear of the desk frame and capacitor. Alternative routes can improve
recovery from a weak neighbor link, but cannot compensate for all links being weak.

The role and partition settings follow
[Espressif's ZigbeeCore documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/zigbee/zigbee_core.html).

## Home Assistant

The device identifies itself as manufacturer **JKCD**, model
**Flexispot-E7Q-H2**. The default build is an always-awake Zigbee router, powered
by USB or the desk. It can route its own traffic through different neighboring
routers and relay traffic for other devices. The optional ED build communicates
through one parent router or coordinator and does not relay other devices' traffic.

| Endpoint | Control | Action when turned ON |
| --- | --- | --- |
| 1 | Stand | Press upstream standing-preset key |
| 2 | Sit | Press upstream sitting-preset key |
| 3 | Preset 1 | Press memory key 1 |
| 4 | Preset 2 | Press memory key 2 |
| 5 | Up | Hold up for 2 seconds by default |
| 6 | Down | Hold down for 2 seconds by default |
| 7 | Memory | Press M; enters save mode |
| 8 | Release | Release emulated keys and discard queued commands |
| 9 | Height | Analog Input sensor, last valid display height in cm |

These are momentary actions implemented with standard **On/Off** clusters.
Firmware advertises the controls as general **On/Off Output** devices (type
`0x0002`), so ZHA discovers momentary **switch** entities without a custom
handler. They do not belong to the `light` domain and are excluded from
domain-wide light actions. The optional ZHA handler presents them as native
**buttons**; the Zigbee2MQTT converter presents them as momentary switches.
ON requests an action and resets to OFF promptly; OFF is a UI state reset and
does not cancel movement. Use endpoint 8 to release keys. OFF reports acknowledge
receipt, **not completed movement**. Commands during the initial ten-second boot
delay or an existing command are dropped; they are not saved for later execution.
Up/down presses cannot be extended by repeated commands while busy.

**Release is not a guaranteed motor emergency stop.** It cancels this bridge's
keypress. A controller may continue an already-triggered preset movement after
key release. Use the physical keypad to interrupt movement as supported by your
controller. Observe the first tests with a clear desk travel path.

Key meanings come from the upstream keypad. On an E7Q with a different keypad,
Stand/Sit may correspond to other numbered presets or be unsupported. Test each
and rename the entities to match your keypad. Save presets with the physical
keypad; automated M/save sequences are deliberately not supplied. Absolute target
height control is not implemented.

### ZHA

#### Firmware-only switches (no custom handler required)

The current firmware exposes standard On/Off Output endpoints, which ZHA
recognizes as **switches**, plus the height sensor. Turn a control ON to press
its desk key; it promptly returns OFF. No custom handler is needed for this
presentation. Home Assistant actions targeting the `light` domain cannot
activate these switch entities. Automations targeting all entities or the
`switch` domain can still activate them; target desk controls deliberately.

**Upgrading from the original light firmware:** ZHA caches endpoint device types.
After flashing the updated firmware, remove the old desk device in ZHA, enable
Add device, then hold the H2's BOOT button for five seconds after startup to
clear its old pairing and pair again. This does not change desk presets. Confirm
that the controls now have `switch.*` entity IDs and remove old light cards or
stale light registry entries. Update automations to use `switch.turn_on`.

Example (replace the entity ID with your actual switch):

```yaml
alias: Desk standing preset
sequence:
  - action: switch.turn_on
    target:
      entity_id: switch.desk_stand
mode: single
```

#### Native buttons (optional custom handler)

The included custom ZHA handler changes the Home Assistant presentation of the
existing firmware. **No reflash or pairing reset is needed.** You get Stand,
Sit, Preset 1, Preset 2, Up, Down, and Release keys as native `button` entities,
alongside the existing height sensor. Memory is an optional configuration button,
disabled by default; enable it from the device's entity list if you need it.

1. Create `/config/custom_zha_quirks` on your **Home Assistant host**.
2. Copy [`integrations/zha/flexispot_e7q_h2.py`](integrations/zha/flexispot_e7q_h2.py)
   into that directory (not the ESP32).
3. Merge this into Home Assistant's `configuration.yaml`. If you already have
   a `zha:` section, add the settings there instead of creating a second section.

   ```yaml
   zha:
     enable_quirks: true
     custom_quirks_path: /config/custom_zha_quirks
   ```

4. Restart Home Assistant. ZHA loads the handler for manufacturer `JKCD`, model
   `Flexispot-E7Q-H2`. Open the desk's ZHA device page and use the new buttons.
5. Replace switch/light cards on your dashboard with the buttons. Previous
   switch/light entities may remain in the entity registry as unavailable; remove
   those stale entries after confirming the buttons work. Update automations to
   use `button.press` rather than `switch.turn_on` (or the original `light.turn_on`).

The handler was tested with `zha-quirks`/ZHA 2.3.0, including serialization of
each button's actual Zigbee command. It includes the older Zigpy v2 builder
import path for compatibility, but that older path has not been validated here.
If the buttons do not appear, check Home Assistant's logs for custom-handler
import errors and confirm the manufacturer/model on the device diagnostics.
Update Home Assistant if its ZHA library lacks the required v2 button API.
Reconfiguring the device can refresh binding/reporting; it does not itself load
a newly installed Python handler, so restart Home Assistant first.

Example Home Assistant script (replace the entity ID with your actual button):

```yaml
alias: Desk standing preset
sequence:
  - action: button.press
    target:
      entity_id: button.desk_stand
mode: single
```

The Release keys button retains the limitations described above: it releases
emulated keys, and is not a guaranteed motor stop. Native buttons do not add
absolute target-height or cover/position control to the firmware.

#### Pairing and generic discovery

1. In Home Assistant, open **Settings → Devices & services → ZHA → Add device**.
2. Power or reboot the H2 near the coordinator. A new device starts network
   steering automatically. If it was previously paired, hold its BOOT button
   for five seconds after startup to clear the pairing and restart.
3. With the handler installed, ZHA presents native buttons and a height sensor.
   Without the handler it discovers eight On/Off **switch** entities and a generic
   Analog Input height sensor. Rename those switches using the endpoint table if
   you choose the generic presentation.
4. Wait at least ten seconds after power-up, then press **Up** (or turn ON its
   generic switch) for the first
   brief movement test. Wake sequencing adds about 1.2 seconds before a keypress.

The height cluster includes a length-prefixed description and BACnet unit 118
(centimeters), and omits the generic Arduino helper's temperature application
type. If an older ZHA installation does not create the sensor, inspect endpoint
9's `analog_input` cluster: `present_value` (0x0055), `description` (0x001c),
and `engineering_units` (0x0075). Read those attributes and reconfigure the device.
Upgrade Home Assistant if its generic analog sensor support is insufficient.

### Zigbee2MQTT

1. Copy [`integrations/zigbee2mqtt/flexispot-e7q-h2.mjs`](integrations/zigbee2mqtt/flexispot-e7q-h2.mjs)
   into Zigbee2MQTT's `external_converters` directory, or use its frontend to
   load the converter. Follow the
   [current external-converter instructions](https://www.zigbee2mqtt.io/advanced/more/external_converters.html).
2. Restart Zigbee2MQTT if installing the file manually. Enable permit join,
   then power/reboot or factory-reset the H2 as described above.
3. With MQTT discovery enabled, Home Assistant gets eight momentary switch
   controls and a height sensor. Turn ON the desired switch to press its key.
   The converter disables unsupported power-on behavior controls.

For MQTT control, replace `desk` with the device's friendly name:

```sh
mosquitto_pub -t zigbee2mqtt/desk/set -m '{"state_up":"ON"}'
mosquitto_pub -t zigbee2mqtt/desk/set -m '{"state_release":"ON"}'
```

The named height endpoint exposes `height_height` with the modern extension's
endpoint suffix. Its reports are in cm. The firmware explicitly reports changes
at most once per second and repeats the last known height every minute.
Height is the **last valid display value**, not a guarantee the UART is still
connected; a sleeping/blank/error display retains that value. Do not treat a
cached height as an independent movement safety signal.

## Onboard RGB status light

The center WS2812 RGB LED uses GPIO8 by default. Its pin and brightness are
configurable in `config.h`; use `STATUS_LED_PIN=-1` to disable it, or set
`STATUS_LED_BRIGHTNESS` from 0 to 255. The default 24 keeps it unobtrusive, and
connected idle green is dimmer still. `STATUS_LED_COLOR_ORDER` defaults to
`LED_COLOR_ORDER_GRB`; change it if your board's pixel uses a different order.
GPIO8 is independent of the desk's wake signal on GPIO12.

| Indication | Meaning |
| --- | --- |
| Dim green | Stack is connected and the coordinator replied within the last two minutes |
| Blinking amber | Waiting for a coordinator reply, or disconnected / reconnecting |
| Three quick purple flashes | A desk command starts transmitting, or Release keys was requested |
| Brief cyan flash | A new valid height differs from the previous reading |
| Red | Invalid GPIO configuration; desk interface disabled |

Purple is triggered by the first active UART command response, after wake timing,
not merely by receipt of a Zigbee request. Repeated controller polls and idle
keepalive packets do not retrigger it. Busy/dropped requests do not flash purple.
Cyan also indicates physical-keypad movement if the resulting height is received;
the first height reading at startup and repeated identical readings do not flash.
Purple takes priority when command and height events overlap. Status returns to
the connection color after the flashes. Height changes can flash while disconnected.

Green requires a recent coordinator response, rather than only the stack's cached
connection flag. It does not confirm Home Assistant processed a report or that
the desk UART is responding. LED timing
uses the normal firmware loop and adds no blink delays to desk control. This is a
local indicator only and does not add a Home Assistant light entity. LED output
uses asynchronous RMT with a persistent payload. If completion does not arrive
within 50 ms, LED updates are disabled until reboot (`led_fail=1` in diagnostics);
the UART and Zigbee loop continue. The watchdog also covers driver initialization.
Updates within the same network role preserve pairing. Switching between ED and
router mode requires the one-time migration described above. Endpoint descriptors
and the optional ZHA handler are identical for both roles.
If Zigbee's initial startup/rejoin wait times out, the firmware keeps its desk
interface, status LED, and reset button running while the initialized Zigbee stack
retries. A stack that never finishes initializing is restarted after five minutes;
a blocked startup call is covered by a separate 60-second watchdog. Neither
recovery clears the saved pairing.

## Troubleshooting and development

USB serial logging is 115200 baud. The desk link is UART1, **9600 8N1**. Set
`DESK_DEBUG_UART=1` for raw RX/TX bytes during bench testing.

- No RX bytes: check common ground, white-blue → RX, wake wiring, spare port,
  and voltage levels. Disconnect brown while debugging from USB.
- Heights arrive but commands do not move the desk: check green → TX and wake;
  inspect `0x11` polls. Movement packets are sent only in response to those polls
  after a 100ms low / 1100ms high wake sequence.
- Display packets are rejected: check 9600 baud and logic levels. The parser
  validates length, terminator, and CRC16/Modbus (big-endian CRC on the wire).
  A different controller protocol needs captured frames and an adaptation.
- Pairing fails: permit join before rebooting; hold BOOT for five seconds to
  clear old network credentials. Ordinary reboot preserves the pairing.
- USB disappears: enter download mode with BOOT/RESET, verify USB CDC is enabled,
  and use the correct USB cable and port.

Host tests (Linux/macOS with a compiler supporting ASan/UBSan):

```sh
bash scripts/test.sh
```

In a sandbox running under a debugger/ptrace, LeakSanitizer may be unavailable;
use `ASAN_OPTIONS=detect_leaks=0 bash scripts/test.sh` in that environment.
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

Native ZHA handler tests (Python 3.12+; use a separate virtual environment):

```sh
python -m pip install -r tests/requirements-zha.txt
python tests/zha_buttons_test.py
python tests/zha_switch_discovery_test.py
```

## Credits and license

The UART protocol, command key mapping, display decoding, and wake sequence are
adapted from Dimitri Sudomoin's MIT-licensed project at commit
`47d6de81b690e9408a351655f83aa7df1b316bc3`. Its copyright and MIT license are
preserved in [`LICENSE`](LICENSE). The bridge replaces ESPHome/Wi-Fi integration
with Zigbee and adds validated framing, bounded requests, and host tests.

APIs and integration behavior were checked against:
[Arduino-ESP32 3.3.6](https://github.com/espressif/arduino-esp32/tree/3.3.6/libraries/Zigbee),
[ZHA's Analog Input sensor](https://github.com/zigpy/zha/blob/dev/zha/application/platforms/sensor/__init__.py),
and [Zigbee2MQTT modern extensions](https://github.com/Koenkk/zigbee-herdsman-converters/blob/master/src/lib/modernExtend.ts).

### Recovery from a lost Zigbee connection

The bridge directly disables Zigbee sleep and enables the receiver while idle,
then reads both settings back. It reasserts and verifies them every minute. This
is necessary because Arduino-ESP32 3.3.6's `setRxOnWhenIdle()` only stores a flag;
the library uses it in a battery-device workaround, not to directly set the
receiver on our mains-powered endpoints. The pinned H2 SDK disables CPU power
management at build time. A no-light-sleep lock also protects future builds where
that feature is enabled. Both router and optional end-device builds remain awake.
Normal connection recovery preserves the saved pairing for the selected role;
it never automatically changes roles or factory-resets the device.

The bridge queries the coordinator once per minute. If it has joined a network
but receives no successful coordinator reply for five minutes, it releases its
emulated desk key and restarts using the saved pairing. An unpaired bridge does
not repeatedly restart. This check does not rely on queued reports or Arduino's
cached connection flag as proof of delivery. Only one query can be outstanding.

Zigbee lock acquisition is nonblocking, so repeated lock contention cannot add
up to long delays in the UART service, status LED, or BOOT button. Failed height/OFF report attempts
are limited to once per second, with pending OFF acknowledgements retained.
A watchdog covers startup with a 60-second limit, then switches to 30 seconds
for the loop if an SDK call stalls. A five-minute initialization deadline covers
a stack that returns from startup but never becomes ready. A functioning unpaired
stack can still wait indefinitely for pairing. These
recoveries preserve Zigbee pairing; holding BOOT remains the explicit reset.
A prolonged coordinator outage can result in another recovery five minutes
after rejoining. In router mode a restart also briefly interrupts traffic being
relayed for other devices. Rebooting releases bridge keys but is not a guaranteed
motor stop for a preset already activated in the desk controller.

If an outage persists, capture the USB serial log and check signal strength,
power, and the coordinator. Once per minute, a `Health:` line includes uptime,
reset reason, current/minimum heap, coordinator reply count and age, verified
awake policy, report queue retries, dropped commands, received UART frames, and
time since the last height frame. Reply age is meaningful after the first reply;
height age is meaningful after a valid height has been received. Queue retry
counts include temporary lock contention, not just radio problems.

Desk UART writes bypass Arduino's blocking write wrapper. With software TX
buffering disabled, a packet is sent only when the previous frame has drained.
Busy polls do not accumulate queued keypresses. Release is retained and retried
before any new active key; a partial write schedules two Release frames to
recover framing. Any short write also cancels the active movement. A transmitter
that stays busy for one second, or repeatedly short-writes three times without a
successful frame, triggers a restart with saved pairing. Recovery/manual reset
wait only bounded intervals to flush the
Release before rebooting. UART write errors and backpressure appear as `tx_err`
and `tx_busy`; the latter can increase during poll floods without indicating a
hardware fault. These checks do not establish that the desk acted on a packet.

Application logging uses a fixed buffer, skips disconnected USB consoles, and
drops log lines rather than waiting for buffer space. Debug logging can still
affect timing when a host is connected; leave raw UART logging off for normal use.
Only eight queued commands are processed per loop, and movement requests older
than one second are dropped. Release remains independent of the command queue
and cannot bypass the ten-second startup guard. Each poll rechecks the movement
deadline using fresh time, including polls received in the same buffered batch.
A wake sequence stalled for more than 2.2 seconds is abandoned rather than
starting a delayed movement. Wake GPIO changes are applied before command logs.

Diagnostic time fields `up_s`, `reply_s`, and `height_s` are in seconds. `retry`
counts report queue retries. `led_fail` flags a disabled/faulted LED driver.

The [2026-10-21 reliability review](docs/reliability-review-2026-10-21.md) records
the findings, changes, and the limits of software-only validation.
