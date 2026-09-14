#!/usr/bin/env bash
# Bootstrap Codespace: activate Nordic toolchain and verify pre-baked NCS.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${REPO_ROOT}/scripts/ncs-env.sh"

export NCS_VERSION="${NCS_VERSION:-v2.9.0}"
export NCS_DIR="${NCS_DIR:-/workdir}"
export NRFUTIL_HOME="${NRFUTIL_HOME:-/usr/local/share/nrfutil}"

echo "==> Repo: ${REPO_ROOT}"
echo "==> Activating nRF toolchain (nrfutil)..."

ncs_activate

echo "==> NCS_DIR=${NCS_DIR}"
echo "==> west check..."
ncs_run 'west --version'

# nordicplayground image already ran west init under /workdir during docker build.
if [[ ! -d "${NCS_DIR}/zephyr" ]]; then
	echo "==> No pre-baked SDK at ${NCS_DIR}, initializing..."
	ncs_run "west init -m https://github.com/nrfconnect/sdk-nrf --mr ${NCS_VERSION} . && west update --narrow -o=--depth=1 && west zephyr-export || true"
else
	echo "==> Using pre-baked / existing SDK at ${NCS_DIR}"
	ncs_run 'west zephyr-export || true'
fi

# Re-activate after possible init
ncs_activate

cat > "${REPO_ROOT}/.ncs_env" <<EOF
export NCS_DIR="${NCS_DIR}"
export NCS_VERSION="${NCS_VERSION}"
export NRFUTIL_HOME="${NRFUTIL_HOME:-/usr/local/share/nrfutil}"
export REPO_ROOT="${REPO_ROOT}"
# shellcheck disable=SC1091
source "${REPO_ROOT}/scripts/ncs-env.sh"
ncs_activate
EOF

chmod +x "${REPO_ROOT}/scripts/"*.sh "${REPO_ROOT}/.devcontainer/"*.sh 2>/dev/null || true

echo
echo "==> Codespace ready."
ncs_run 'west --version'
echo "    Build with: ./scripts/build-promicro.sh"
echo
