/*!
 * \file key_gpio.c
 * \brief GPIO 按键：已配对时唤醒/发键；未配对忽略。
 *
 * 上电配对窗内若仍有旧凭证，按键会先重连一次；成功则停止配对并发键。
 */

#include "key_gpio.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(key_gpio, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
static const struct gpio_dt_spec key_sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static bool key_ready;
static bool last_pressed;
static int64_t last_edge_ms;
#define KEY_DEBOUNCE_MS 30
#define KEY_SEND_NAME "a"
#endif

int key_gpio_init(void)
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
	if (!gpio_is_ready_dt(&key_sw)) {
		LOG_WRN("sw0 GPIO not ready");
		return -ENODEV;
	}

	if (gpio_pin_configure_dt(&key_sw, GPIO_INPUT) != 0) {
		LOG_WRN("sw0 configure failed");
		return -EIO;
	}

	key_ready = true;
	last_pressed = gpio_pin_get_dt(&key_sw) > 0;
	last_edge_ms = k_uptime_get();
	LOG_INF("Key GPIO ready (sw0), press sends '%s'", KEY_SEND_NAME);
	return 0;
#else
	LOG_WRN("No sw0 alias — physical key disabled");
	return 0;
#endif
}

void key_gpio_poll(struct app_ctx *ctx)
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
	bool pressed;
	int64_t now;
	enum unifying_error err;

	if (!key_ready || ctx == NULL) {
		return;
	}

	now = k_uptime_get();
	if (now - last_edge_ms < KEY_DEBOUNCE_MS) {
		return;
	}

	pressed = gpio_pin_get_dt(&key_sw) > 0;
	if (pressed == last_pressed) {
		return;
	}

	last_pressed = pressed;
	last_edge_ms = now;

	if (!pressed) {
		return;
	}

	/* 未连接且未配对：忽略（配对窗内亦同） */
	if (ctx->mode != APP_MODE_CONNECTED && !ctx->has_credentials) {
		LOG_INF("Key ignored: not paired");
		return;
	}

	/* app_do_key：有凭证则重连；配对窗内重连成功才停止 pair */
	err = app_do_key(ctx, KEY_SEND_NAME);
	ctx->last_error = err;
	if (err) {
		LOG_WRN("Key send failed: %s", unifying_get_error_name(err));
	}
#else
	ARG_UNUSED(ctx);
#endif
}
