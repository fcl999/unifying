# ZMK + Logitech Unifying 示例

在 Pro Micro nRF52840 / Nice!Nano 上运行 **ZMK**，并用按键在下列输出间互斥切换：

| Fn 层按键 | 输出 |
|-----------|------|
| Q / W / E | 蓝牙 profile 0 / 1 / 2 |
| R | Logitech Unifying |
| T | USB |

**优联 ↔ 蓝牙/USB** 通过写入 settings 后 **软复位** 切换，避免 nRF52840 上 BLE 与 ESB 抢 RADIO。

## ESB 是什么

**ESB（Enhanced ShockBurst）** 是 Nordic 射频芯片上的一套 **2.4GHz 专有链路协议**（不是蓝牙）。

- 和 nRF24L01 一类芯片同族思路：短包、ACK、自动重传、固定地址/信道
- Logitech **Unifying** 跑在兼容 ESB/nRF24 的物理层之上（再加自己的帧格式与 AES）
- 本仓库用 Nordic **`CONFIG_ESB`**（来自 sdk-nrf）当射频驱动；BLE 也用同一块 RADIO，所以二者不能同时开

关系可以记成：

```text
按键 HID  →  Unifying 协议库  →  ESB 射频  →  优联接收器  →  电脑
                 (本仓库 src/)     (Nordic)      (Logitech)
```

## 优联模式启动（对齐 promicro）

进入优联并重启后，逻辑与 [`examples/promicro_nrf52840`](../promicro_nrf52840/) 一致：

| 条件 | 行为 |
|------|------|
| 已有配对凭证 | 先自动 `connect` 重连 |
| 无凭证，或重连失败 | 开启约 **20s** 配对窗，每 **2s** 自动 `pair` |
| 配对成功 | 写入 settings，进入已连接，可打字 |
| 配对窗超时 | 停止自动配对（需再次切到优联或调用 `&uni_pair` 强制重试） |

使用前请先让 **Unifying 接收器进入配对**（按接收器上的配对键），再 Fn+R 进优联（或已在优联窗内）。

`&uni_pair` / `&uni_unpair` 仍保留在模块里，供强制重配或清凭证，**keymap 默认不再绑定 Combo**。

## 目录结构

```text
examples/zmk_unifying/
  config/           # zmk-config（keymap / west.yml）
  module/           # ZMK out-of-tree 模块（优联 transport + behaviors）
  scripts/          # 构建脚本
  zmk/              # 已克隆的上游 ZMK（git clone；可被 west update 管理）
  nrf/              # west update 后出现（sdk-nrf，提供 CONFIG_ESB）
  zephyr/           # west update 后出现（ZMK 使用的 Zephyr）
```

协议实现复用仓库根目录 [`src/`](../../src/)（`unifying_*.c`）。

## 依赖

1. [ZMK 工具链](https://zmk.dev/docs/development/local-toolchain/setup/install)（west、Zephyr SDK、Python）
2. 本目录下的 ZMK（已预置 `git clone`，或由 `west update` 拉取）
3. `sdk-nrf`（west.yml 中 `path: nrf`，**仅作模块、不 import**，用于 ESB）

> 纯上游 Zephyr **没有** `CONFIG_ESB`。没有 `nrf` 模块时优联无法编译。

## 获取 / 更新 ZMK

```bash
cd examples/zmk_unifying
# 若尚无 zmk 目录：
git clone --depth 1 https://github.com/zmkfirmware/zmk.git zmk

west init -l config   # 仅首次
west update           # 拉取 zephyr、nrf、hal 等
```

当前预置克隆版本可在 `zmk/` 下用 `git log -1` 查看。

## 编译

Linux / macOS / Git Bash：

```bash
./scripts/build.sh
# 或指定板型：
BOARD=nice_nano_v2 ./scripts/build.sh
```

Windows（已配置 west 环境）：

```bat
scripts\build.bat
```

手动：

```bash
cd examples/zmk_unifying
west build -s zmk/app -b nice_nano_v2 -d build/nice_nano_v2 -p -- \
  -DSHIELD=promicro_uni_proto \
  -DZMK_CONFIG="$PWD/config" \
  -DZMK_EXTRA_MODULES="$PWD/module"
```

产物一般在 `build/<board>/zephyr/zmk.uf2`（视 board 而定）。

### SuperMini / Pro Micro UF2 @ 0x27000

若使用 Adafruit / SuperMini S140 v7 bootloader，应用必须从 `0x27000` 链接。可参考 [`module/pm_static.yml`](module/pm_static.yml) 与 `examples/promicro_nrf52840` 的分区说明；具体 board 需在 NCS/分区管理器或 board DTS 中对齐，否则会出现「刷完又回呼吸灯」现象。

## 接线（原型 shield）

默认 1×6 矩阵（可按硬件改 `module/boards/shields/promicro_uni_proto/promicro_uni_proto.overlay`）：

- Row：`P0.02`
- Col：`P0.09` … `P0.14` → 键位 Q W E R T Fn

## 使用步骤

1. 刷入固件，默认 **STD 模式**（USB/BLE，与普通 ZMK 相同）。
2. **Fn+T**：USB 出键；**Fn+Q/W/E**：三路蓝牙选机并配对。
3. 接收器按配对键 → **Fn+R** 进优联并重启 → 自动重连或 20s 内自动配对。
4. 配对成功后即可打字；**Fn+Q/W/E/T** 可软复位回 STD 并选中对应输出。

## 模块 API（摘要）

| Behavior | 参数 | 作用 |
|----------|------|------|
| `&uni_mode` | `UNI_OUT` | 进入优联并重启 |
| `&uni_mode` | `UNI_TO_BLE0..2` / `UNI_TO_USB` | 回到 STD（必要时重启）并选输出 |
| `&uni_pair` | `UNI_PAIR` / `UNI_UNPAIR` | 强制配对 / 清除凭证（可选绑定） |

## 已知限制

- 优联与 BLE **不能同时占用 RADIO**；切换必有约 1–2s 重启中断。
- 首版不包含优联鼠标/多媒体 report、热切换；配对窗超时后不会像 promicro 那样关 USB 深度休眠。
- `sdk-nrf` 与 ZMK 所用 Zephyr 版本若 API 不兼容，需调整 `config/west.yml` 中 `sdk-nrf` 的 `revision`。
- 仅用于自有 Unifying 接收器测试，请遵守当地无线电法规。

## 对照示例

非 ZMK、纯 Zephyr/NCS CLI 测试固件仍见：[`examples/promicro_nrf52840/`](../promicro_nrf52840/)。
