/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zmk_unifying/mode.h>
#include <zmk_unifying/persist.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <zmk/endpoints.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#endif

LOG_MODULE_REGISTER(zmk_uni_mode, CONFIG_ZMK_LOG_LEVEL);

static enum zmk_unifying_mode current_mode = ZMK_UNIFYING_MODE_STD;
static uint8_t boot_pending;

enum zmk_unifying_mode zmk_unifying_mode_get(void)
{
	return current_mode;
}

bool zmk_unifying_mode_is_unifying(void)
{
	return current_mode == ZMK_UNIFYING_MODE_UNIFYING;
}

int zmk_unifying_mode_set_and_reboot(enum zmk_unifying_mode mode)
{
	int err;

	err = zmk_unifying_persist_save_mode((uint8_t)mode);
	if (err) {
		return err;
	}

	LOG_INF("reboot to mode %d", (int)mode);
	k_msleep(50);
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

static void apply_pending_output(uint8_t pending)
{
	switch (pending) {
	case ZMK_UNI_PENDING_USB:
		(void)zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_USB);
		break;
#if IS_ENABLED(CONFIG_ZMK_BLE)
	case ZMK_UNI_PENDING_BLE0:
		(void)zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
		(void)zmk_ble_prof_select(0);
		break;
	case ZMK_UNI_PENDING_BLE1:
		(void)zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
		(void)zmk_ble_prof_select(1);
		break;
	case ZMK_UNI_PENDING_BLE2:
		(void)zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
		(void)zmk_ble_prof_select(2);
		break;
#endif
	default:
		break;
	}

	(void)zmk_unifying_persist_save_pending(ZMK_UNI_PENDING_NONE);
}

static void pending_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (boot_pending != ZMK_UNI_PENDING_NONE) {
		apply_pending_output(boot_pending);
		boot_pending = ZMK_UNI_PENDING_NONE;
	}
}

static K_WORK_DELAYABLE_DEFINE(pending_work, pending_work_handler);

static int mode_boot_init(void)
{
	uint8_t mode = ZMK_UNIFYING_MODE_STD;

	(void)zmk_unifying_persist_init();
	(void)zmk_unifying_persist_load_mode(&mode);
	(void)zmk_unifying_persist_load_pending(&boot_pending);
	current_mode = (enum zmk_unifying_mode)mode;
	LOG_INF("boot transport mode=%d pending=%u", (int)current_mode, boot_pending);

	if (current_mode == ZMK_UNIFYING_MODE_STD && boot_pending != ZMK_UNI_PENDING_NONE) {
		k_work_schedule(&pending_work, K_MSEC(500));
	}

	return 0;
}

SYS_INIT(mode_boot_init, APPLICATION, 40);
