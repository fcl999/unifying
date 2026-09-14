#!/usr/bin/env bash
# Build Pro Micro nRF52840 Unifying firmware using nRF Connect SDK.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="${REPO_ROOT}/examples/promicro_nrf52840"
BUILD_DIR="${APP_DIR}/build"
PM_STATIC="${APP_DIR}/pm_static.yml"

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
echo "    pm_static=${PM_STATIC}"

run_build() {
	local board="$1"
	local cmd
	# Force Adafruit UF2 flash map so app links at 0x26000 (SoftDevice S140 gap).
	cmd="west build -p always -b ${board} ${APP_DIR} -d ${BUILD_DIR} -- -DPM_STATIC_YML_FILE=${PM_STATIC}"
	ncs_run "${cmd}"
}

verify_load_offset() {
	local cfg="${BUILD_DIR}/zephyr/.config"
	local offset=""

	if [[ -f "${BUILD_DIR}/partitions.yml" ]]; then
		echo "==> partitions.yml (app region):"
		grep -A6 '^app:' "${BUILD_DIR}/partitions.yml" || true
	fi

	if [[ -f "${cfg}" ]]; then
		offset="$(grep -E '^CONFIG_FLASH_LOAD_OFFSET=' "${cfg}" | cut -d= -f2 || true)"
		echo "==> CONFIG_FLASH_LOAD_OFFSET=${offset:-<missing>}"
		if [[ -n "${offset}" && "${offset}" != "0x26000" && "${offset}" != "155648" ]]; then
			echo "ERROR: flash load offset is not 0x26000 — UF2 will boot-loop back to bootloader."
			echo "        Do not flash this image. Check pm_static.yml / board target."
			return 1
		fi
	fi

	if [[ -f "${BUILD_DIR}/zephyr/zephyr.uf2" ]]; then
		echo "==> UF2 ready: ${BUILD_DIR}/zephyr/zephyr.uf2"
		# Optional: parse first UF2 block target address if Python available
		python3 - <<'PY' "${BUILD_DIR}/zephyr/zephyr.uf2" || true
import struct, sys
path = sys.argv[1]
with open(path, "rb") as f:
    block = f.read(512)
if len(block) < 32 or block[0:4] != b"UF2\n":
    print("WARN: not a UF2 file")
    raise SystemExit(0)
# UF2 block: magic0, magic1, flags, target_addr, payload_size, block_no, num_blocks, family_id
target = struct.unpack_from("<I", block, 12)[0]
family = struct.unpack_from("<I", block, 28)[0]
print(f"==> UF2 first target_addr=0x{target:08X} family=0x{family:08X}")
if target != 0x26000:
    print("ERROR: UF2 target address must be 0x26000 for Adafruit/Nice!Nano SoftDevice boards")
    raise SystemExit(1)
print("OK: UF2 start address matches SoftDevice gap")
PY
	else
		echo "WARN: zephyr.uf2 not produced — check CONFIG_BUILD_OUTPUT_UF2"
	fi
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
verify_load_offset

echo
echo "==> Build finished (board=${BOARD})."
if [[ -f "${BUILD_DIR}/zephyr/zephyr.uf2" ]]; then
	echo "    Flash ONLY this file via UF2 drive:"
	echo "    ${BUILD_DIR}/zephyr/zephyr.uf2"
fi
if [[ -f "${BUILD_DIR}/zephyr/zephyr.hex" ]]; then
	echo "    HEX: ${BUILD_DIR}/zephyr/zephyr.hex"
fi
