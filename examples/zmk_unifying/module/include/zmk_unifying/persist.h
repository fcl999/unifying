/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "unifying_const.h"

#ifdef __cplusplus
extern "C" {
#endif

struct zmk_unifying_credentials {
	uint8_t address[UNIFYING_ADDRESS_LEN];
	uint8_t aes_key[UNIFYING_AES_BLOCK_LEN];
	uint32_t aes_counter;
};

/** 软复位后在 STD 模式下执行的挂起动作 */
enum zmk_unifying_pending {
	ZMK_UNI_PENDING_NONE = 0,
	ZMK_UNI_PENDING_USB = 1,
	ZMK_UNI_PENDING_BLE0 = 2,
	ZMK_UNI_PENDING_BLE1 = 3,
	ZMK_UNI_PENDING_BLE2 = 4,
};

int zmk_unifying_persist_init(void);
bool zmk_unifying_persist_load(struct zmk_unifying_credentials *out);
int zmk_unifying_persist_save(const struct zmk_unifying_credentials *in);
int zmk_unifying_persist_clear(void);

int zmk_unifying_persist_load_mode(uint8_t *mode);
int zmk_unifying_persist_save_mode(uint8_t mode);

int zmk_unifying_persist_load_pending(uint8_t *pending);
int zmk_unifying_persist_save_pending(uint8_t pending);

#ifdef __cplusplus
}
#endif
