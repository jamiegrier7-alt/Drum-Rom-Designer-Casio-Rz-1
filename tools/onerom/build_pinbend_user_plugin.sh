#!/usr/bin/env bash
set -euo pipefail

# Build the pinbend plugin as a One ROM USER plugin using the upstream plugin.mk.
# Usage:
#   tools/onerom/build_pinbend_user_plugin.sh /path/to/one-rom/sdrr/ora
# or set ONE_ROM_ORA_DIR env var.

ORA_DIR="${1:-${ONE_ROM_ORA_DIR:-}}"
if [[ -z "${ORA_DIR}" ]]; then
  echo "error: provide path to one-rom sdrr/ora dir as arg or ONE_ROM_ORA_DIR env var" >&2
  echo "example: tools/onerom/build_pinbend_user_plugin.sh ~/src/one-rom/sdrr/ora" >&2
  exit 1
fi

if [[ ! -f "${ORA_DIR}/plugin.mk" ]]; then
  echo "error: ${ORA_DIR}/plugin.mk not found" >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/onerom_pinbend_plugin"
mkdir -p "${OUT_DIR}"

make -f "${ORA_DIR}/plugin.mk" \
  ORA_INCLUDE="${ORA_DIR}" \
  PLUGIN_TYPE=USER \
  SRC="${ROOT_DIR}/src/onerom_pinbend_plugin_main.c" \
  BUILD_DIR="${OUT_DIR}"

echo "Built plugin:" >&2
ls -1 "${OUT_DIR}"/plugin_user.*
