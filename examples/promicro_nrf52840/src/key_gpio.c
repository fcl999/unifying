/*!
 * \file key_gpio.c
 * \brief GPIOTE 中断按键 + 休眠唤醒信号。
 */

#include "key_gpio.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(key_gpio, CONFIG_LOG_DEFAULT_LEVEL);

K_SEM_DEFINE(key_wake_sem, 0, 1);

#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
static const struct gpio_dt_spec key_sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback key_cb;
static atomic_t key_irq_latched;
static int64_t last_accept_ms;
static bool key_ready;

#define KEY_DEBOUNCE_MS 30
#define KEY_SEND_NAME "a"

static void key_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	atomic_set(&key_irq_latched, 1);
	k_sem_give(&key_wake_sem);
}
#endif

int key_gpio_init(void)
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
	int err;

	if (!gpio_is_ready_dt(&key_sw)) {
		LOG_WRN("sw0 GPIO not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&key_sw, GPIO_INPUT);
	if (err) {
		LOG_WRN("sw0 configure failed: %d", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&key_sw, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_WRN("sw0 interrupt configure failed: %d", err);
		return err;
	}

	gpio_init_callback(&key_cb, key_isr, BIT(key_sw.pin));
	err = gpio_add_callback(key_sw.port, &key_cb);
	if (err) {
		LOG_WRN("sw0 add callback failed: %d", err);
		return err;
	}

	key_ready = true;
	last_accept_ms = 0;
	atomic_set(&key_irq_latched, 0);
	LOG_INF("Key GPIOTE ready (sw0), press sends '%s'", KEY_SEND_NAME);
	return 0;
#else
	LOG_WRN("No sw0 alias — physical key disabled");
	return 0;
#endif
}

void key_gpio_clear_wake(void)
{
	while (k_sem_take(&key_wake_sem, K_NO_WAIT) == 0) {
	}
}

void key_gpio_wait_wake(void)
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
	/* 若 ISR 已锁存，直接返回，避免 clear+take 吞掉信号导致死等 */
	if (atomic_get(&key_irq_latched)) {
		return;
	}
#endif
	(void)k_sem_take(&key_wake_sem, K_FOREVER);
}

void key_gpio_service(struct app_ctx *ctx)
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
	int64_t now;
	enum unifying_error err;

	if (!key_ready || ctx == NULL) {
		return;
	}

	if (!atomic_cas(&key_irq_latched, 1, 0)) {
		return;
	}

	now = k_uptime_get();
	if (now - last_accept_ms < KEY_DEBOUNCE_MS) {
		return;
	}

	/* 边沿后确认仍为按下 */
	if (gpio_pin_get_dt(&key_sw) <= 0) {
		return;
	}

	last_accept_ms = now;

	if (ctx->mode != APP_MODE_CONNECTED && !ctx->has_credentials) {
		LOG_INF("Key ignored: not paired");
		return;
	}

	err = app_key_request(ctx, KEY_SEND_NAME);
	ctx->last_error = err;
	if (err) {
		LOG_WRN("Key request failed: %s", unifying_get_error_name(err));
	}
#else
	ARG_UNUSED(ctx);
#endif
}
