#!/usr/bin/env bash
# Bootstrap nRF Connect SDK west workspace for Codespaces / Dev Containers.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NCS_VERSION="${NCS_VERSION:-v2.9.0}"
NCS_DIR="${NCS_DIR:-${HOME}/ncs}"

echo "==> Repo: ${REPO_ROOT}"
echo "==> NCS:  ${NCS_DIR} (${NCS_VERSION})"

mkdir -p "$(dirname "${NCS_DIR}")"

if [[ ! -d "${NCS_DIR}/.west" ]]; then
  echo "==> Initializing west workspace (first run may take a long time)..."
  mkdir -p "${NCS_DIR}"
  cd "${NCS_DIR}"
  west init -m https://github.com/nrfconnect/sdk-nrf --mr "${NCS_VERSION}"
  west update --narrow -o=--depth=1
  if command -v west >/dev/null 2>&1; then
    west zephyr-export || true
  fi
  if [[ -f zephyr/scripts/requirements.txt ]]; then
    pip3 install --user -r zephyr/scripts/requirements.txt || true
  fi
  if [[ -f nrf/scripts/requirements.txt ]]; then
    pip3 install --user -r nrf/scripts/requirements.txt || true
  fi
else
  echo "==> Existing NCS workspace found, skipping west init"
fi

cat > "${REPO_ROOT}/.ncs_env" <<EOF
export NCS_DIR="${NCS_DIR}"
export NCS_VERSION="${NCS_VERSION}"
export REPO_ROOT="${REPO_ROOT}"
EOF

chmod +x "${REPO_ROOT}/scripts/build-promicro.sh" 2>/dev/null || true

echo
echo "==> Codespace ready."
echo "    Build firmware with:"
echo "      ./scripts/build-promicro.sh"
echo "    Artifacts land in examples/promicro_nrf52840/build/zephyr/"
echo
