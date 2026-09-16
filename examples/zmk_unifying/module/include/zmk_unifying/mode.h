/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zmk_unifying_mode {
	ZMK_UNIFYING_MODE_STD = 0,      /* USB / BLE (ZMK 默认) */
	ZMK_UNIFYING_MODE_UNIFYING = 1, /* Logitech Unifying (ESB) */
};

enum zmk_unifying_mode zmk_unifying_mode_get(void);
bool zmk_unifying_mode_is_unifying(void);

/**
 * 写入模式并冷重启。跨 BLE↔优联时必须重启以释放 RADIO。
 */
int zmk_unifying_mode_set_and_reboot(enum zmk_unifying_mode mode);

#ifdef __cplusplus
}
#endif
