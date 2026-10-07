#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CLI="${ARDUINO_CLI:-arduino-cli}"
FQBN="esp32:esp32:esp32h2:ZigbeeMode=ed,PartitionScheme=zigbee,CDCOnBoot=cdc,FlashSize=4M"
"$CLI" compile --fqbn "$FQBN" --warnings all --output-dir build \
  firmware/flexispot_zigbee "$@"
