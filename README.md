# Flexispot E7Q Zigbee bridge

Firmware for a **Super Mini ESP32-H2**, adapting the desk protocol from
[dimitri-vs/flexispot-esphome](https://github.com/dimitri-vs/flexispot-esphome)
to native Zigbee. Pair it with a Home Assistant **ZHA** or **Zigbee2MQTT**
coordinator. No Wi-Fi, ESPHome server, or MQTT client runs on the H2.

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
| Green | 6 | GPIO11 | UART TX: H2 → desk |
| White-blue | 5 | GPIO12 | UART RX: desk → H2 |
| Blue | 4 | GPIO13 | Wake / keypad enable |

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
#define DESK_TX_PIN 11
#define DESK_RX_PIN 12
#define DESK_WAKE_PIN 13
#define RESET_BUTTON_PIN 9
#define DESK_DISPLAY_IN_INCHES 0
#define DESK_NUDGE_MS 500
#define DESK_PRESET_HOLD_MS 1000
#define DESK_DEBUG_UART 0
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
4. Set **Zigbee mode → Zigbee ED (end device)**,
   **Partition Scheme → Zigbee 4MB with spiffs**, **Flash Size → 4MB**,
   and **USB CDC On Boot → Enabled**. Verify your board actually has 4MB flash.
5. Upload. If it does not enter the bootloader automatically, hold BOOT, tap
   RESET, release BOOT, then upload. Reboot afterward.

### Arduino CLI

```sh
arduino-cli core install esp32:esp32@3.3.6 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
bash scripts/build.sh
arduino-cli upload \
  --fqbn 'esp32:esp32:esp32h2:ZigbeeMode=ed,PartitionScheme=zigbee,CDCOnBoot=cdc,FlashSize=4M' \
  --port /dev/ttyACM0 --input-dir build firmware/flexispot_zigbee
```

Replace the port with your board's port. `ARDUINO_CLI` can override the executable.
For example, compile with different GPIOs without editing defaults:

```sh
bash scripts/build.sh --build-property \
  'compiler.cpp.extra_flags=-DDESK_TX_PIN=11 -DDESK_RX_PIN=12 -DDESK_WAKE_PIN=13'
```

For a **2MB** H2 board use `FlashSize=2M,PartitionScheme=zigbee_2MB` instead
of the 4MB settings (both when compiling and uploading). Do not flash a 4MB
partition layout to a 2MB board. The build workflow uses the 4MB default and
uploads its binaries as a GitHub Actions artifact.

## Home Assistant

The device identifies itself as manufacturer **JKCD**, model
**Flexispot-E7Q-H2**. It is an always-awake Zigbee end device, powered by USB or
the desk. It does not extend your Zigbee mesh as a router.

| Endpoint | Control | Action when turned ON |
| --- | --- | --- |
| 1 | Stand | Press upstream standing-preset key |
| 2 | Sit | Press upstream sitting-preset key |
| 3 | Preset 1 | Press memory key 1 |
| 4 | Preset 2 | Press memory key 2 |
| 5 | Up | Hold up for 500ms by default |
| 6 | Down | Hold down for 500ms by default |
| 7 | Memory | Press M; enters save mode |
| 8 | Release | Release emulated keys and discard queued commands |
| 9 | Height | Analog Input sensor, last valid display height in cm |

These are momentary actions implemented with standard **On/Off** clusters.
The included ZHA handler presents them as native **button** entities; the
Zigbee2MQTT converter presents them as momentary switches.
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

#### Native buttons (recommended)

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
5. Remove old light cards from your dashboard and replace them with the buttons.
   Previous light entities may remain in the entity registry as unavailable;
   remove those stale entries after confirming the buttons work. Update existing
   automations to use `button.press` rather than `light.turn_on`.

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
   Without the handler it discovers eight On/Off **light** entities and a generic
   Analog Input height sensor. Rename those lights using the endpoint table if
   you choose the generic presentation.
4. Wait at least ten seconds after power-up, then press **Up** (or turn ON its
   generic light) for the first
   brief movement test. Wake sequencing adds about 1.2 seconds before a keypress.

The height cluster includes a length-prefixed description and BACnet unit 118
(centimeters), and omits the generic Arduino helper's temperature application
type. If an older ZHA installation does not create the sensor, inspect endpoint
9's `analog_input` cluster: `present_value` (0x0055), `description` (0x001c),
and `engineering_units` (0x0075). Read those attributes and reconfigure the device.
Upgrade Home Assistant if its generic analog sensor support is insufficient.

Example script for the generic light presentation, without the handler:

```yaml
alias: Desk standing preset
sequence:
  - action: light.turn_on
    target:
      entity_id: light.desk_stand
mode: single
```

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
