# Copyright (c) 2026
# SPDX-License-Identifier: MIT

zephyr_library()

if(CONFIG_ZMK_UNIFYING)
  set(UNIFYING_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)

  zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${UNIFYING_LIB_DIR}
  )

  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

  zephyr_library_sources(
    src/mode.c
    src/persist.c
    src/radio_esb.c
    src/transport.c
    src/hid_mirror.c
    src/behavior_unifying_mode.c
    src/behavior_unifying_pair.c
    ${UNIFYING_LIB_DIR}/unifying.c
    ${UNIFYING_LIB_DIR}/unifying_data.c
    ${UNIFYING_LIB_DIR}/unifying_state.c
    ${UNIFYING_LIB_DIR}/unifying_buffer.c
    ${UNIFYING_LIB_DIR}/unifying_utils.c
    ${UNIFYING_LIB_DIR}/unifying_const.c
    ${UNIFYING_LIB_DIR}/unifying_error.c
    ${UNIFYING_LIB_DIR}/aes.c
  )

  zephyr_library_compile_definitions(UNIFYING_HARDWARE_AES=0)
endif()
