#!/usr/bin/env bash
# Build Pro Micro nRF52840 Unifying firmware using nRF Connect SDK.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="${REPO_ROOT}/examples/promicro_nrf52840"
BUILD_DIR="${APP_DIR}/build"

# shellcheck disable=SC1091
source "${REPO_ROOT}/scripts/ncs-env.sh"

if [[ -f "${REPO_ROOT}/.ncs_env" ]]; then
	# shellcheck disable=SC1091
	source "${REPO_ROOT}/.ncs_env"
else
	ncs_activate
fi

BOARD="${BOARD:-promicro_nrf52840/nrf52840/uf2}"

if [[ ! -d "${NCS_DIR}/zephyr" && ! -d "${NCS_DIR}/.west" ]]; then
	echo "ERROR: NCS workspace not found at ${NCS_DIR}"
	echo "Run: bash .devcontainer/post-create.sh"
	exit 1
fi

echo "==> Building ${APP_DIR}"
echo "    board=${BOARD}"
echo "    ncs=${NCS_DIR}"

run_build() {
	local board="$1"
	local cmd
	if [[ "${USE_STORAGE_OVERLAY:-0}" == "1" && -f "${APP_DIR}/storage.overlay" ]]; then
		cmd="west build -p always -b ${board} ${APP_DIR} -d ${BUILD_DIR} -- -DEXTRA_DTC_OVERLAY_FILE=${APP_DIR}/storage.overlay"
	else
		cmd="west build -p always -b ${board} ${APP_DIR} -d ${BUILD_DIR}"
	fi
	ncs_run "${cmd}"
}

set +e
run_build "${BOARD}"
STATUS=$?
set -e

if [[ ${STATUS} -ne 0 && "${BOARD}" == promicro_nrf52840* ]]; then
	echo
	echo "==> Build failed with ${BOARD}, retrying nice_nano_v2..."
	BOARD="nice_nano_v2"
	run_build "${BOARD}"
fi

echo
echo "==> Build finished (board=${BOARD})."
if [[ -f "${BUILD_DIR}/zephyr/zephyr.uf2" ]]; then
	echo "    UF2: ${BUILD_DIR}/zephyr/zephyr.uf2"
fi
if [[ -f "${BUILD_DIR}/zephyr/zephyr.hex" ]]; then
	echo "    HEX: ${BUILD_DIR}/zephyr/zephyr.hex"
fi
if [[ -f "${BUILD_DIR}/zephyr/zephyr.elf" ]]; then
	echo "    ELF: ${BUILD_DIR}/zephyr/zephyr.elf"
fi
