@echo off
REM Windows 构建入口（需已安装 west + Zephyr/ZMK 工具链）
set ROOT=%~dp0..
set BOARD=%BOARD%
if "%BOARD%"=="" set BOARD=nice_nano_v2
set SHIELD=%SHIELD%
if "%SHIELD%"=="" set SHIELD=promicro_uni_proto

cd /d "%ROOT%"

if not exist zmk\app (
  echo Clone ZMK first: git clone --depth 1 https://github.com/zmkfirmware/zmk.git zmk
  exit /b 1
)

if not exist .west\config (
  west init -l config
)

west update
west build -s zmk/app -b %BOARD% -d build\%BOARD% -p -- -DSHIELD=%SHIELD% -DZMK_CONFIG=%ROOT%\config -DZMK_EXTRA_MODULES=%ROOT%\module
