/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "unifying.h"

#ifdef __cplusplus
extern "C" {
#endif

int radio_esb_init(struct unifying_interface *interface);
void radio_esb_sleep(void);
int radio_esb_wake(void);
bool radio_esb_is_sleeping(void);
uint8_t radio_esb_current_channel(void);
uint8_t radio_esb_last_channel(void);

#ifdef __cplusplus
}
#endif
