/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_unifying_pair

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <dt-bindings/zmk_unifying/unifying.h>
#include <zmk_unifying/mode.h>
#include <zmk_unifying/transport.h>

#include <zmk/behavior.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
				     struct zmk_behavior_binding_event event)
{
	enum unifying_error err;

	ARG_UNUSED(event);

	if (!zmk_unifying_mode_is_unifying()) {
		LOG_WRN("pair ignored: not in Unifying mode");
		return -EAGAIN;
	}

	switch (binding->param1) {
	case UNI_PAIR:
		err = zmk_unifying_transport_pair();
		return (err == UNIFYING_SUCCESS) ? 0 : -EIO;
	case UNI_UNPAIR:
		err = zmk_unifying_transport_unpair();
		return (err == UNIFYING_SUCCESS) ? 0 : -EIO;
	default:
		return -ENOTSUP;
	}
}

static const struct behavior_driver_api behavior_unifying_pair_driver_api = {
	.binding_pressed = on_keymap_binding_pressed,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
			&behavior_unifying_pair_driver_api);

#endif
