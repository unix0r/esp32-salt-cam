#!/usr/bin/env bash
# Run ESPHome from the pinned container image so the toolchain is reproducible.
#   scripts/esphome.sh config  [saltcam-tank1.yaml]
#   scripts/esphome.sh compile [saltcam-tank1.yaml]
#   scripts/esphome.sh run     [saltcam-tank1.yaml] --device /dev/ttyUSB0   # first flash over USB
#   scripts/esphome.sh logs    [saltcam-tank1.yaml] --device /dev/ttyUSB0
set -euo pipefail

ESPHOME_VERSION="2026.9.0"
IMAGE="ghcr.io/esphome/esphome:${ESPHOME_VERSION}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmd="${1:?usage: esphome.sh <config|compile|run|logs|...> [yaml] [args]}"
shift
yaml="saltcam-tank1.yaml"
if [[ $# -gt 0 && "$1" == *.yaml ]]; then
  yaml="$1"
  shift
fi

# Pass through a serial device (`--device /dev/ttyUSB0`) to the container for USB flashing.
podman_args=()
esphome_args=()
while [[ $# -gt 0 ]]; do
  if [[ "$1" == "--device" && $# -ge 2 && "$2" == /dev/tty* ]]; then
    podman_args+=(--device "$2" --group-add keep-groups)
    esphome_args+=(--device "$2")
    shift 2
  else
    esphome_args+=("$1")
    shift
  fi
done

tty_args=()
if [[ -t 0 && -t 1 ]]; then
  tty_args=(-it)
fi

exec podman run --rm "${tty_args[@]}" \
  -v "${ROOT}:/config:Z" \
  -v saltcam-platformio:/root/.platformio \
  "${podman_args[@]}" \
  "${IMAGE}" "${cmd}" "${yaml}" "${esphome_args[@]}"
