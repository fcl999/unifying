#!/usr/bin/env bash
# Build Pro Micro nRF52840 Unifying firmware using nRF Connect SDK.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="${REPO_ROOT}/examples/promicro_nrf52840"
BUILD_DIR="${APP_DIR}/build"

if [[ -f "${REPO_ROOT}/.ncs_env" ]]; then
  # shellcheck disable=SC1091
  source "${REPO_ROOT}/.ncs_env"
fi

NCS_DIR="${NCS_DIR:-${HOME}/ncs}"
BOARD="${BOARD:-promicro_nrf52840/nrf52840/uf2}"

if [[ ! -d "${NCS_DIR}/.west" ]]; then
  echo "ERROR: NCS west workspace not found at ${NCS_DIR}"
  echo "Run: bash .devcontainer/post-create.sh"
  exit 1
fi

if ! command -v west >/dev/null 2>&1; then
  echo "ERROR: west not found in PATH"
  exit 1
fi

echo "==> Building ${APP_DIR}"
echo "    board=${BOARD}"
echo "    ncs=${NCS_DIR}"

cd "${NCS_DIR}"

EXTRA_ARGS=()
if [[ -f "${APP_DIR}/storage.overlay" && "${USE_STORAGE_OVERLAY:-0}" == "1" ]]; then
  EXTRA_ARGS+=(-- "-DEXTRA_DTC_OVERLAY_FILE=${APP_DIR}/storage.overlay")
fi

set +e
west build -p always -b "${BOARD}" "${APP_DIR}" -d "${BUILD_DIR}" "${EXTRA_ARGS[@]}"
STATUS=$?
set -e

if [[ ${STATUS} -ne 0 ]]; then
  echo
  echo "==> Build failed with ${BOARD}, retrying nice_nano_v2..."
  BOARD="nice_nano_v2"
  west build -p always -b "${BOARD}" "${APP_DIR}" -d "${BUILD_DIR}"
fi

echo
echo "==> Build finished."
if [[ -f "${BUILD_DIR}/zephyr/zephyr.uf2" ]]; then
  echo "    UF2: ${BUILD_DIR}/zephyr/zephyr.uf2"
fi
if [[ -f "${BUILD_DIR}/zephyr/zephyr.hex" ]]; then
  echo "    HEX: ${BUILD_DIR}/zephyr/zephyr.hex"
fi
if [[ -f "${BUILD_DIR}/zephyr/zephyr.elf" ]]; then
  echo "    ELF: ${BUILD_DIR}/zephyr/zephyr.elf"
fi
