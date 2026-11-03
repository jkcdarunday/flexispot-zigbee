#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CLI="${ARDUINO_CLI:-arduino-cli}"
# The SDK library and partition layout must match the requested role.
case "${ZIGBEE_ROLE:-router}" in
  router) zigbee_mode=zczr; zigbee_partitions=zigbee_zczr ;;
  end-device) zigbee_mode=ed; zigbee_partitions=zigbee ;;
  *) printf 'ZIGBEE_ROLE must be router or end-device\n' >&2; exit 2 ;;
esac
FQBN="esp32:esp32:esp32h2:ZigbeeMode=${zigbee_mode},PartitionScheme=${zigbee_partitions},CDCOnBoot=cdc,FlashSize=4M"
"$CLI" compile --fqbn "$FQBN" --warnings all --output-dir build \
  firmware/flexispot_zigbee "$@"
