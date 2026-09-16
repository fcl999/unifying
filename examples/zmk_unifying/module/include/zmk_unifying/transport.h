/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "unifying_const.h"
#include "unifying_error.h"

#ifdef __cplusplus
extern "C" {
#endif

int zmk_unifying_transport_init(void);
bool zmk_unifying_transport_ready(void);
bool zmk_unifying_transport_has_credentials(void);
bool zmk_unifying_transport_connected(void);

enum unifying_error zmk_unifying_transport_pair(void);
enum unifying_error zmk_unifying_transport_unpair(void);
enum unifying_error zmk_unifying_transport_connect(void);

/**
 * 发送 6 键 HKRO 报告（USB HID scancode + modifiers）。
 */
enum unifying_error zmk_unifying_transport_send_keys(const uint8_t keys[UNIFYING_KEYS_LEN],
						     uint8_t modifiers);

void zmk_unifying_transport_tick(void);

#ifdef __cplusplus
}
#endif
