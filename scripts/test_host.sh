#!/usr/bin/env bash
# Build and run the host-side unit tests inside the pinned ESPHome image (it ships g++).
#   scripts/test_host.sh
set -euo pipefail

ESPHOME_VERSION="2026.9.0"
IMAGE="ghcr.io/esphome/esphome:${ESPHOME_VERSION}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec podman run --rm --entrypoint sh -v "${ROOT}:/src:ro,Z" "${IMAGE}" -c '
  set -e
  cd /tmp
  g++ -std=c++17 -Wall -Wextra -Werror \
    -I/src/components/salt_level \
    /src/tests/host/test_image_analysis.cpp /src/components/salt_level/image_analysis.cpp \
    -o test_image_analysis
  ./test_image_analysis
'
