# Copyright (c) 2026
# SPDX-License-Identifier: MIT
#
# Out-of-tree NCS/Zephyr application for Pro Micro nRF52840 Unifying tests.
# Build from an nRF Connect SDK workspace:
#   west build -b promicro_nrf52840/nrf52840/uf2 <path-to>/examples/promicro_nrf52840

cmake_minimum_required(VERSION 3.20.0)

# Use the SuperMini S140 v7 UF2 + SoftDevice flash map.
set(PM_STATIC_YML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/pm_static.yml CACHE FILEPATH
    "Static partition map for SuperMini S140 v7 UF2" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(promicro_unifying)

set(UNIFYING_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${UNIFYING_SRC_DIR}
)

target_sources(app PRIVATE
  src/main.c
  src/radio_esb.c
  src/usb_cli.c
  src/persist.c
  src/keymap.c
  ${UNIFYING_SRC_DIR}/unifying.c
  ${UNIFYING_SRC_DIR}/unifying_data.c
  ${UNIFYING_SRC_DIR}/unifying_state.c
  ${UNIFYING_SRC_DIR}/unifying_buffer.c
  ${UNIFYING_SRC_DIR}/unifying_utils.c
  ${UNIFYING_SRC_DIR}/unifying_const.c
  ${UNIFYING_SRC_DIR}/unifying_error.c
  ${UNIFYING_SRC_DIR}/aes.c
)
