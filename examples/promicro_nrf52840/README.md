# Pro Micro nRF52840 — Unifying 测试固件

在 [Pro Micro nRF52840](https://docs.zephyrproject.org/latest/boards/others/promicro_nrf52840/doc/index.html)
（Nice!Nano / SuperMini 同系）上，用片上 RADIO + Nordic **ESB** 接入本仓库的 Unifying 协议库，
并通过 **USB CDC 串口** 控制配对、休眠、重连与按键发送。

> 需要 **nRF Connect SDK**（含 `CONFIG_ESB`）。上游纯 Zephyr 没有 ESB，无法直接编译本示例。

## 功能

| 能力 | 命令 / 行为 |
|------|-------------|
| 配对 | `pair` → `unifying_pair()`，凭证写入 NVS |
| 按键 | `key a` / `key shift+a` → 加密按键按下+松开 |
| 主动休眠 | `sleep` → 停 keep-alive，关闭 RADIO |
| 休眠重连 | `wake` → `unifying_connect()` wake-up |
| 断电重连 | 上电自动读 NVS 并 `connect`，再开串口看 `status` |

## 硬件与依赖

- 板子：`promicro_nrf52840`（或同引脚的 Nice!Nano 克隆板）
- 罗技 Unifying 接收器（自用测试）
- PC：Windows 串口助手 / PuTTY（115200 8N1）
- SDK：nRF Connect SDK **2.9+** 或 **3.x**（需有该板型；没有可用 `nice_nano_v2`）

**不要启用 Bluetooth**：RADIO 与 ESB 互斥。出厂 SoftDevice / UF2 可保留，应用侧 `CONFIG_BT=n`。

## 编译

### GitHub Codespaces（推荐云端编译）

1. 在 GitHub 打开本仓库 → **Code** → **Codespaces** → **Create codespace on main**
2. 机器类型选可用的即可（如 2-core）；不要选「无」
3. 首次创建会拉取 [nordicplayground/nrfconnect-sdk:v2.9-branch](https://hub.docker.com/r/nordicplayground/nrfconnect-sdk) 并执行 `west update`（可能需 10–20 分钟；磁盘紧张时可在创建后清理）
4. 终端执行：

```bash
./scripts/build-promicro.sh
```

产物：`examples/promicro_nrf52840/build/zephyr/zephyr.uf2`（或 `.hex`）。可从 Codespaces 下载后 UF2 烧录。

可选环境变量：

```bash
BOARD=nice_nano_v2 ./scripts/build-promicro.sh
USE_STORAGE_OVERLAY=1 ./scripts/build-promicro.sh
```

### 本地 west

在 NCS 的 west workspace 中执行（把路径换成你的仓库位置）：

```bat
west build -b promicro_nrf52840/nrf52840/uf2 D:\wlt\project\test\unifying\examples\promicro_nrf52840 -p always
```

若板型名不同，可试：

```bat
west build -b promicro_nrf52840/nrf52840 D:\wlt\project\test\unifying\examples\promicro_nrf52840
west build -b nice_nano_v2 D:\wlt\project\test\unifying\examples\promicro_nrf52840
```

产物：

- UF2：`build/zephyr/zephyr.uf2`（uf2 目标）
- 或 HEX：`build/zephyr/zephyr.hex`

### NVS / storage 分区

默认依赖 DTS / Partition Manager 的 `storage_partition`。若链接报找不到该分区，可追加 overlay：

```bat
west build -b promicro_nrf52840/nrf52840/uf2 ... -- -DEXTRA_DTC_OVERLAY_FILE=storage.overlay
```

若与板级分区冲突，删除或不要使用 `storage.overlay`。

### 较新 Zephyr USB 栈（NCS 3 / Zephyr 4）

若 `CONFIG_USB_DEVICE_STACK` 已废弃，请把 [`prj.conf`](prj.conf) 中 USB 段改成：

```conf
CONFIG_USB_DEVICE_STACK_NEXT=y
CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_UART_LINE_CTRL=y
```

并去掉旧的 `CONFIG_USB_CDC_ACM` / `CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT` 等项。

## 烧录（UF2）

1. 快速短接 **GND–RST 两次**，LED 进入呼吸/渐变 → 出现 U 盘
2. 将 `zephyr.uf2` 拷入该盘
3. 板子复位后，PC 应枚举出 **USB 串口（CDC ACM）**

也可用 `west flash`（需 J-Link / pyocd，见板级文档）。

## 串口命令

打开对应 COM 口，115200，发送行以 `\n` 结尾。

| 命令 | 说明 |
|------|------|
| `help` | 帮助 |
| `status` | `mode` / 是否已配对 / 信道 / 上次错误 |
| `pair` | 先让接收器进入配对，再执行 |
| `sleep` | 主动休眠 |
| `wake` | 唤醒并重连 |
| `key <name>` | 发送按键，如 `key a`、`key enter`、`key ctrl+c` |
| `unpair` | 擦除 NVS 凭证 |

成功回 `OK`，失败回 `ERR ...`。

### 支持的按键名

- `a`–`z`、`0`–`9`
- `enter` `esc` `space` `tab` `bspc`
- `f1`–`f12`、方向键 `left` `right` `up` `down`
- 修饰键组合：`shift+a`、`ctrl+c`、`alt+tab`、`gui+e`

## 测试步骤

### 1. 配对

1. 插入 Unifying 接收器，按下接收器配对按钮
2. 串口发送：`pair`
3. 成功应 `OK`；罗技软件中可能出现设备名 **ProMicroKB**
4. `status` 应显示 `mode=connected paired=yes`

### 2. 按键

1. 焦点放在记事本
2. 发送 `key a`、`key shift+a`、`key enter`
3. 主机应打出对应字符

### 3. 主动休眠

1. `sleep` → `OK`，`status` 为 `sleeping`
2. 一段时间后接收器侧设备应掉线（无 keep-alive）

### 4. 休眠后重连

1. `wake` → `OK`，`status` 为 `connected`
2. 再 `key a` 应恢复输入

### 5. 断电重连

1. 已配对后拔掉 USB（或按 RST）
2. 再插电；固件自动 `connect`（无需先开串口，keep-alive 仍会跑）
3. 打开串口，`status` 应为 `connected`
4. `key a` 可用

## 架构说明

```
PC 串口 ──USB CDC──► usb_cli ──► app 状态机
                                   │
                                   ▼
                            unifying_* API
                                   │
                                   ▼
                            radio_esb (ESB PTX)
                                   │
                                   ▼
                            Unifying 接收器
```

协议库源码直接编译自仓库 [`../../src`](../../src)（不含桌面空 `main.c`）。
射频参数对齐 Arduino 示例：2 Mbps、CRC-16、5 字节地址、动态长度、ACK、自动重传。

## LED

板载 `led0`（文档为 P0.15）：

- 已连接：常亮
- 休眠 / idle：灭
- 配对 / 发键：闪烁

## 故障排查

| 现象 | 排查 |
|------|------|
| `pair` 一直失败 | 接收器是否在配对态；距离；地址字节序；重传参数 |
| 编译找不到 `esb.h` | 必须在 NCS 环境，不能只用上游 Zephyr |
| 无 COM 口 | 确认 CDC overlay；驱动；是否进了 UF2 而不是应用 |
| NVS 失败 | 检查 `storage_partition` / 使用 `storage.overlay` |
| 配对成功但不能打字 | AES 密钥是否正确保存；`status` 是否 connected；试 `wake` |

仅用于测试自有接收器与自制外设，请遵守当地无线电法规。
