/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_unifying_mode

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <dt-bindings/zmk_unifying/unifying.h>
#include <zmk_unifying/mode.h>
#include <zmk_unifying/persist.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int leave_to_std_with_pending(uint8_t pending)
{
	int err;

	err = zmk_unifying_persist_save_pending(pending);
	if (err) {
		return err;
	}

	if (zmk_unifying_mode_is_unifying()) {
		return zmk_unifying_mode_set_and_reboot(ZMK_UNIFYING_MODE_STD);
	}

	/* 已在 STD：直接切输出 */
	switch (pending) {
	case ZMK_UNI_PENDING_USB:
		(void)zmk_unifying_persist_save_pending(ZMK_UNI_PENDING_NONE);
		return zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_USB);
#if IS_ENABLED(CONFIG_ZMK_BLE)
	case ZMK_UNI_PENDING_BLE0:
		(void)zmk_unifying_persist_save_pending(ZMK_UNI_PENDING_NONE);
		(void)zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
		return zmk_ble_prof_select(0);
	case ZMK_UNI_PENDING_BLE1:
		(void)zmk_unifying_persist_save_pending(ZMK_UNI_PENDING_NONE);
		(void)zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
		return zmk_ble_prof_select(1);
	case ZMK_UNI_PENDING_BLE2:
		(void)zmk_unifying_persist_save_pending(ZMK_UNI_PENDING_NONE);
		(void)zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
		return zmk_ble_prof_select(2);
#endif
	default:
		(void)zmk_unifying_persist_save_pending(ZMK_UNI_PENDING_NONE);
		return 0;
	}
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
				     struct zmk_behavior_binding_event event)
{
	ARG_UNUSED(event);

	switch (binding->param1) {
	case UNI_STD:
		return leave_to_std_with_pending(ZMK_UNI_PENDING_NONE);
	case UNI_OUT:
		(void)zmk_unifying_persist_save_pending(ZMK_UNI_PENDING_NONE);
		return zmk_unifying_mode_set_and_reboot(ZMK_UNIFYING_MODE_UNIFYING);
	case UNI_TO_BLE0:
		return leave_to_std_with_pending(ZMK_UNI_PENDING_BLE0);
	case UNI_TO_BLE1:
		return leave_to_std_with_pending(ZMK_UNI_PENDING_BLE1);
	case UNI_TO_BLE2:
		return leave_to_std_with_pending(ZMK_UNI_PENDING_BLE2);
	case UNI_TO_USB:
		return leave_to_std_with_pending(ZMK_UNI_PENDING_USB);
	default:
		LOG_ERR("unknown unifying mode cmd %d", binding->param1);
		return -ENOTSUP;
	}
}

static const struct behavior_driver_api behavior_unifying_mode_driver_api = {
	.binding_pressed = on_keymap_binding_pressed,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
			&behavior_unifying_mode_driver_api);

#endif
