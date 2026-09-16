#!/usr/bin/env bash
# 在 examples/zmk_unifying 下构建 Pro Micro Unifying 原型固件。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BOARD="${BOARD:-nice_nano_v2}"
SHIELD="${SHIELD:-promicro_uni_proto}"

if [[ ! -d zmk/app ]]; then
  echo "ZMK 尚未就绪。请先："
  echo "  west init -l config && west update"
  echo "或: git clone --depth 1 https://github.com/zmkfirmware/zmk.git zmk"
  exit 1
fi

if [[ ! -f .west/config ]]; then
  echo "==> west init -l config"
  west init -l config
fi

echo "==> west update（拉取 Zephyr / sdk-nrf 等）"
west update

echo "==> build BOARD=$BOARD SHIELD=$SHIELD"
west build -s zmk/app -b "$BOARD" -d build/$BOARD -p -- \
  -DSHIELD="$SHIELD" \
  -DZMK_CONFIG="$ROOT/config" \
  -DZMK_EXTRA_MODULES="$ROOT/module"

echo
echo "产物（若支持 UF2）："
find "build/$BOARD" -name '*.uf2' -o -name 'zmk.hex' 2>/dev/null | head -20
echo "OK"
