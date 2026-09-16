/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 *
 * 优联模式下：延迟读取 ZMK 共享 HID 缓冲并发送 Unifying 帧。
 * 启动时将 preferred transport 设为 NONE，避免 USB/BLE 双发。
 */

#include <zmk_unifying/mode.h>
#include <zmk_unifying/transport.h>

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#include "unifying_const.h"
#include "unifying_error.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_UNIFYING)

static void copy_hkro_keys(uint8_t out[UNIFYING_KEYS_LEN],
			   const struct zmk_hid_keyboard_report_body *body)
{
	memset(out, 0, UNIFYING_KEYS_LEN);

#if IS_ENABLED(CONFIG_ZMK_HID_REPORT_TYPE_HKRO)
	size_t n = CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE;
	if (n > UNIFYING_KEYS_LEN) {
		n = UNIFYING_KEYS_LEN;
	}
	memcpy(out, body->keys, n);
#else
	size_t filled = 0;
	for (size_t bit = 0; bit <= ZMK_HID_KEYBOARD_NKRO_MAX_USAGE && filled < UNIFYING_KEYS_LEN;
	     bit++) {
		size_t byte = bit / 8;
		uint8_t mask = BIT(bit % 8);
		if (body->keys[byte] & mask) {
			out[UNIFYING_KEYS_LEN - 1 - filled] = (uint8_t)bit;
			filled++;
		}
	}
#endif
}

static void flush_unifying_report(void)
{
	struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
	uint8_t keys[UNIFYING_KEYS_LEN];
	enum unifying_error err;

	copy_hkro_keys(keys, &report->body);
	err = zmk_unifying_transport_send_keys(keys, report->body.modifiers);
	if (err != UNIFYING_SUCCESS) {
		LOG_DBG("unifying send: %d", (int)err);
	}
}

static void flush_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	flush_unifying_report();
}

static K_WORK_DELAYABLE_DEFINE(uni_flush_work, flush_work_handler);

static int unifying_hid_mirror_sched(const zmk_event_t *eh)
{
	const struct zmk_keycode_state_changed *ev;

	if (!zmk_unifying_mode_is_unifying() || !zmk_unifying_transport_ready()) {
		return ZMK_EV_EVENT_BUBBLE;
	}

	ev = as_zmk_keycode_state_changed(eh);
	if (ev == NULL || ev->usage_page != HID_USAGE_KEY) {
		return ZMK_EV_EVENT_BUBBLE;
	}

	k_work_schedule(&uni_flush_work, K_MSEC(1));
	return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(unifying_hid_mirror, unifying_hid_mirror_sched);
ZMK_SUBSCRIPTION(unifying_hid_mirror, zmk_keycode_state_changed);

#endif /* CONFIG_ZMK_UNIFYING */
