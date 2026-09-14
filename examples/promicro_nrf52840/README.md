# Pro Micro nRF52840 — Unifying 测试固件

在 [Pro Micro nRF52840](https://docs.zephyrproject.org/latest/boards/others/promicro_nrf52840/doc/index.html)
（Nice!Nano / SuperMini 同系）上，用片上 RADIO + Nordic **ESB** 接入本仓库的 Unifying 协议库，
并通过 **USB CDC 串口** 控制配对、休眠、重连与按键发送。

> 需要 **nRF Connect SDK**（含 `CONFIG_ESB`）。上游纯 Zephyr 没有 ESB，无法直接编译本示例。

## 重要：优联不是蓝牙

本固件走 **Logitech Unifying（ESB 私有协议）**，**不会**出现在蓝牙扫描列表里。

- 罗技 Unifying 接收器 / 罗技软件 **不会**像 BLE 那样“扫描到未配对设备”
- 必须：接收器按配对键 → PC 串口发 `pair` → 射频完成握手后才会出现设备（名称约 `ProMicroKB`）
- **判断固件是否刷成功，看 USB 串口，不要看接收器能不能扫到**

## 功能

| 能力 | 命令 / 行为 |
|------|-------------|
| 配对 | `pair` → `unifying_pair()`，凭证写入 NVS |
| 按键 | `key a` / `key shift+a` → 加密按键按下+松开 |
| 主动休眠 | `sleep` → 停 keep-alive，关 RADIO |
| 休眠重连 | `wake` → `unifying_connect()` wake-up |
| 断电重连 | 上电自动读 NVS 并 `connect`，再开串口看 `status` |

## 硬件与依赖

- 板子：`promicro_nrf52840`（或同引脚的 Nice!Nano / SuperMini 克隆板）
- 罗技 Unifying 接收器（自用测试）
- PC：Windows 串口助手 / PuTTY（**115200 8N1**，换行建议发 `\n`）
- SDK：nRF Connect SDK **3.4.0**，必须包含 `promicro_nrf52840` board 定义。
- 本项目不使用 `nice_nano_v2` fallback；若找不到该板型，应先修复 SDK 环境。

**不要启用 Bluetooth**：RADIO 与 ESB 互斥。出厂 SoftDevice / UF2 bootloader 可保留，应用侧 `CONFIG_BT=n`。

## 如何确认固件已刷入（必读）

刷入成功后，**唯一可靠依据是 USB CDC 串口 CLI**，不是 LED 颜色，也不是优联接收器。

### 检查清单

1. **UF2 拷贝是否完成**
   - 双击 RST 进入 bootloader：出现 U 盘（常见名 `NICENANO` / `NRF52BOOT` 等），**红灯呼吸/渐变**
   - 把 `zephyr.uf2` 拖进 U 盘；拷贝结束后 U 盘应自动弹出，板子重启
   - 若 U 盘一直不消失、或文件仍在盘里，说明没刷进去

2. **重启后应出现串口（成功标志）**
   - 设备管理器里出现新的 **COM 口**（USB 串行设备 / CDC ACM）
   - 设备名称可能含 **`ProMicro Unifying`**
   - VID/PID（本工程配置）：**`1915:520F`**（Nordic VID + 自定义 PID）
   - 用串口助手打开该 COM：**115200**，应看到类似：

     ```text
     === ProMicro Unifying CLI ===
     Type 'help' for commands.
     Boot status: mode=... paired=... last_err=...
     ```

   - 发送 `help` 再回车，应打印命令列表并以 `OK` 结尾  
   - 发送 `status`，应打印 `mode=idle|connected|...`

3. **若没有 COM 口 / 没有上述 banner**
   - **本固件未在跑**（刷写失败、刷了别的 uf2、或仍停在 bootloader）
   - 见下方 [LED 含义](#led-含义) 与 [故障排查](#故障排查)

### 你描述的现象（拷完又红灯呼吸、无串口）

| 步骤 | 现象 | 含义 |
|------|------|------|
| 双击 RST | 红灯渐变 + U 盘 | bootloader 正常 |
| 拖入 uf2 | U 盘消失 | bootloader **接收了文件**（不等于应用能跑） |
| 紧接着 | **红灯又渐变** + 蓝灯闪 | 应用**启动失败**，又回到 bootloader；或闪一下后跑旧程序 |
| 重新上电 | 仅蓝灯闪、**无新 COM** | **本固件未在运行**（仍是旧 BLE/其它程序，或空应用区） |

这几乎总是 **UF2 链接地址不对**：SuperMini 的 S140 7.3.0 占 `0x00000–0x27000`，应用必须从 **`0x27000`** 开始。
用 NCS 默认 Partition Manager 编出来的 uf2 常从 `0x0`/`0x1000` 起，bootloader 写完后跳转失败 → 你看到的正是这种现象。

**处理：**

1. 拉最新代码，用脚本重编（已带 [`pm_static.yml`](pm_static.yml)）：

   ```bash
   ./scripts/build-promicro.sh
   ```

2. 编译结束应打印类似：

   ```text
   CONFIG_FLASH_LOAD_OFFSET=0x27000
   UF2 first target_addr=0x00027000
   OK: UF2 start address matches SuperMini S140 v7 gap
   ```

   若不是 `0x27000`，**不要刷**。

3. 只刷新生成的 `examples/promicro_nrf52840/build/promicro_nrf52840/zephyr/zephyr.uf2`

4. 成功标志：上电后**红灯不再持续呼吸**，PC 出现 COM，串口有 `=== ProMicro Unifying CLI ===`

### 其它现象解读

| 现象 | 更可能的含义 |
|------|----------------|
| 刷写时红灯渐变 + 出 U 盘 | 正常 bootloader |
| 重启后蓝灯一直闪、无 COM | 未跑本固件（旧固件或刷写地址错误） |
| 优联接收器“扫描不到” | 正常；优联不是 BLE，需串口 `pair` |

**下一步：** 用带 `pm_static.yml` 的新 uf2 重刷，先通串口再配对。

## 编译

### GitHub Codespaces（推荐云端编译）

1. 打开仓库 → **Code** → **Codespaces** → **Create codespace on main**
2. 机器类型选可用项（如 2-core），不要选「无」
3. 初始化会激活 `/root/ncs/v3.4.0` toolchain；脚本不会使用 NCS 2.9 的 `/workdir`，也不会自动切换到其他 board
4. 若 `postCreate` 失败，终端执行：`bash .devcontainer/post-create.sh`
5. 编译：

```bash
./scripts/build-promicro.sh
```

产物：`examples/promicro_nrf52840/build/promicro_nrf52840/zephyr/zephyr.uf2`。脚本会检查 **UF2 起始地址必须是 `0x27000`**。

> 不要再依赖 `storage.overlay` 改分区；存储区已写在 [`pm_static.yml`](pm_static.yml) 里。

### 本地 west

```bat
west build -b promicro_nrf52840/nrf52840/uf2 <仓库>\examples\promicro_nrf52840 -p always
```

板型固定为 `promicro_nrf52840/nrf52840/uf2`，不使用其他 board fallback。

### NVS / 分区

NCS 下用 [`pm_static.yml`](pm_static.yml) 固定 SuperMini UF2 + S140 v7 布局（应用 `@0x27000`）。
不要用会覆盖整片 flash 分区的随意 overlay，以免再次刷不进应用。

### 较新 USB 栈（NCS 3 / Zephyr 4）

若旧 `CONFIG_USB_DEVICE_STACK` 不可用，见 `prj.conf` 注释，改为 `CONFIG_USB_DEVICE_STACK_NEXT` + `CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT`。

## 烧录（UF2）逐步

1. USB 插入 PC
2. **快速短接 GND–RST 两次** → 红灯呼吸，出现 UF2 U 盘
3. 将 **本仓库编译出的** `zephyr.uf2` 拷入（不要用错文件）
4. 等待自动复位（U 盘消失）
5. **按上面的检查清单确认 COM 口 + CLI banner**
6. 再进行优联配对（下一节）

也可用调试器：`west flash`（J-Link / pyocd）。

## 串口命令

打开 COM，115200 8N1，每行以换行结束。

| 命令 | 说明 |
|------|------|
| `help` | 帮助 |
| `status` | `mode` / 是否已配对 / 信道 / 上次错误 |
| `pair` | **先**让接收器进入配对，再执行 |
| `sleep` | 主动休眠 |
| `wake` | 唤醒并重连 |
| `key <name>` | 按键，如 `key a`、`key enter`、`key ctrl+c` |
| `unpair` | 擦除 NVS 凭证 |

成功 `OK`，失败 `ERR ...`。

### 按键名

- `a`–`z`、`0`–`9`
- `enter` `esc` `space` `tab` `bspc`
- `f1`–`f12`、`left` `right` `up` `down`
- 组合：`shift+a`、`ctrl+c`、`alt+tab`、`gui+e`

## 测试步骤（串口确认后再做）

### 1. 配对

1. PC 串口已能 `help` / `status`
2. 插入 Unifying 接收器，**按接收器上的配对按钮**（指示灯按接收器说明闪烁）
3. 串口发送：`pair`
4. 成功：`OK`；`status` → `mode=connected paired=yes`
5. 可选：罗技 Unifying 软件中查看是否出现 **ProMicroKB**

### 2. 按键

记事本聚焦后：`key a`、`key shift+a`、`key enter`。

### 3. 主动休眠

`sleep` → `status` 为 `sleeping`；一段时间后主机侧设备应掉线。

### 4. 休眠重连

`wake` → `connected` → 再 `key a`。

### 5. 断电重连

已配对后拔 USB / 按 RST → 再插电 → 开串口 `status` 应为 `connected`。

## 架构

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

协议源码来自 [`../../src`](../../src)。射频：2 Mbps、CRC-16、5 字节地址、动态长度、ACK、自动重传。

## LED 含义

克隆板红/蓝灯可能对应不同 GPIO；以行为为准。

| 状态 | 典型表现 |
|------|----------|
| UF2 bootloader | **红灯呼吸/渐变**，出 U 盘 |
| 本固件 idle / 未配对 / sleep | **灯灭**（或极暗） |
| 本固件已连接优联 | **常亮**（`led0`，文档脚 P0.15，颜色因板而异） |
| 本固件配对中 / 发键 | **短闪几下**，不是一直闪 |
| 出厂 BLE / 其它固件 | 常见 **蓝灯持续闪烁** ← 说明还在跑旧程序 |

## 故障排查

| 现象 | 处理 |
|------|------|
| U 盘消失后又红灯呼吸、无 COM | **链接地址错**：用 `./scripts/build-promicro.sh` 重编，确认 UF2 `@0x27000` 再刷 |
| 蓝灯一直闪、无 COM | 同上；确认刷的是新生成的 `zephyr.uf2` |
| 有 U 盘但拷完不重启 | 换线/口；确认文件是 uf2 |
| 有 COM 但无 banner | 打开串口后发回车；确认 115200；勾选 DTR |
| `help` 无响应 | 换行用 LF；确认 COM 口 |
| 接收器“扫不到” | **不要扫**。先串口通，再 `pair` |
| `pair` 失败 | 接收器配对键；贴近；看 `status` 的 `last_err` |
| VID/PID 不是 1915:520F | 仍是 bootloader 或其它固件 |
| 编译无 `esb.h` | 必须在 NCS / Codespaces 中编 |
| NVS 报错 | 使用仓库内 `pm_static.yml`（已含 storage） |

### Windows 快速看 USB 设备

- 设备管理器 → 端口 (COM 和 LPT) → 是否有新 COM  
- 或 PowerShell：

```powershell
Get-PnpDevice -Class Ports | Format-Table -AutoSize
# 可选：查看 USB 描述（需安装对应工具）或在“通用串行总线设备”里找 ProMicro Unifying
```

仅用于测试自有接收器与自制外设，请遵守当地无线电法规。
