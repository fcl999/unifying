/*!
 * \file main.c
 * \brief Pro Micro nRF52840 Unifying 测试固件入口与状态机。
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

#include "app_ctx.h"
#include "keymap.h"
#include "persist.h"
#include "radio_esb.h"
#include "usb_cli.h"
#include "unifying.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define TRANSMIT_BUFFER_SIZE 8
#define RECEIVE_BUFFER_SIZE 8
#define DEVICE_NAME "ProMicroKB"
#define DEVICE_TYPE 0x0147
#define CAPABILITIES 0x1E40

#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static bool led_ready;
#else
static bool led_ready;
#endif

static struct app_ctx g_ctx;

const char *app_mode_name(enum app_mode mode)
{
	switch (mode) {
	case APP_MODE_IDLE:
		return "idle";
	case APP_MODE_PAIRING:
		return "pairing";
	case APP_MODE_CONNECTED:
		return "connected";
	case APP_MODE_SLEEPING:
		return "sleeping";
	default:
		return "unknown";
	}
}

static void led_set(bool on)
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
	if (!led_ready) {
		return;
	}

	gpio_pin_set_dt(&led, on ? 1 : 0);
#else
	ARG_UNUSED(on);
#endif
}

static void led_blink(uint32_t times)
{
	uint32_t i;

	for (i = 0; i < times; i++) {
		led_set(true);
		k_msleep(40);
		led_set(false);
		k_msleep(40);
	}

	if (g_ctx.mode == APP_MODE_CONNECTED) {
		led_set(true);
	}
}

static void led_init(void)
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
	if (!device_is_ready(led.port)) {
		LOG_WRN("LED not ready");
		return;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_WRN("LED configure failed");
		return;
	}

	led_ready = true;
#else
	LOG_WRN("No led0 alias");
#endif
}

static uint32_t random_u32(void)
{
	return sys_rand32_get();
}

static void save_credentials(struct app_ctx *ctx)
{
	struct persist_credentials cred;

	memcpy(cred.address, ctx->address, UNIFYING_ADDRESS_LEN);
	memcpy(cred.aes_key, ctx->aes_key, UNIFYING_AES_BLOCK_LEN);
	cred.aes_counter = ctx->aes_counter;
	if (persist_save(&cred) == 0) {
		ctx->has_credentials = true;
	}
}

static void state_bind(struct app_ctx *ctx)
{
	unifying_state_init(&ctx->state,
			    &ctx->interface,
			    ctx->transmit_buffer,
			    ctx->receive_buffer,
			    ctx->address,
			    ctx->aes_key,
			    ctx->aes_counter,
			    UNIFYING_DEFAULT_TIMEOUT_KEYBOARD,
			    unifying_channels[0]);
}

enum unifying_error app_do_pair(struct app_ctx *ctx)
{
	enum unifying_error err;
	uint8_t id = (uint8_t)random_u32();
	uint16_t product_id = (uint16_t)(random_u32() & 0xFFFF);
	uint32_t crypto = random_u32();
	uint32_t serial = random_u32();
	const char *name = DEVICE_NAME;
	uint8_t name_length = (uint8_t)strlen(name);

	if (radio_esb_is_sleeping()) {
		err = radio_esb_wake();
		if (err) {
			return UNIFYING_ERROR;
		}
	}

	ctx->mode = APP_MODE_PAIRING;
	led_blink(3);
	unifying_state_buffers_clear(&ctx->state);

	err = unifying_pair(&ctx->state,
			    id,
			    product_id,
			    DEVICE_TYPE,
			    crypto,
			    serial,
			    CAPABILITIES,
			    name,
			    name_length);
	if (err) {
		ctx->mode = APP_MODE_IDLE;
		led_set(false);
		return err;
	}

	ctx->aes_counter = ctx->state.aes_counter;
	save_credentials(ctx);
	ctx->mode = APP_MODE_CONNECTED;
	led_set(true);
	LOG_INF("Paired OK");
	return UNIFYING_SUCCESS;
}

enum unifying_error app_do_connect(struct app_ctx *ctx)
{
	enum unifying_error err;

	if (!ctx->has_credentials) {
		return UNIFYING_ERROR;
	}

	if (radio_esb_is_sleeping()) {
		if (radio_esb_wake() != 0) {
			return UNIFYING_ERROR;
		}
	}

	/* 确保射频使用已保存地址 */
	if (ctx->interface.set_address(ctx->address) != 0) {
		return UNIFYING_SET_ADDRESS_ERROR;
	}

	if (ctx->interface.set_channel(unifying_channels[0]) != 0) {
		return UNIFYING_SET_CHANNEL_ERROR;
	}

	ctx->state.channel = unifying_channels[0];
	unifying_state_buffers_clear(&ctx->state);

	err = unifying_connect(&ctx->state);
	if (err) {
		ctx->mode = APP_MODE_IDLE;
		led_set(false);
		return err;
	}

	ctx->mode = APP_MODE_CONNECTED;
	led_set(true);
	LOG_INF("Connected");
	return UNIFYING_SUCCESS;
}

enum unifying_error app_do_sleep(struct app_ctx *ctx)
{
	if (ctx->mode != APP_MODE_CONNECTED && ctx->mode != APP_MODE_IDLE) {
		/* 允许从 connected 休眠；idle 也可直接关射频 */
	}

	unifying_state_buffers_clear(&ctx->state);
	radio_esb_sleep();
	ctx->mode = APP_MODE_SLEEPING;
	led_set(false);
	LOG_INF("Sleeping");
	return UNIFYING_SUCCESS;
}

enum unifying_error app_do_wake(struct app_ctx *ctx)
{
	enum unifying_error err;

	if (radio_esb_wake() != 0) {
		return UNIFYING_ERROR;
	}

	if (!ctx->has_credentials) {
		ctx->mode = APP_MODE_IDLE;
		return UNIFYING_ERROR;
	}

	err = app_do_connect(ctx);
	return err;
}

enum unifying_error app_do_key(struct app_ctx *ctx, const char *name)
{
	struct keymap_stroke stroke;
	enum unifying_error err;
	uint8_t release_keys[UNIFYING_KEYS_LEN] = {0};

	if (ctx->mode != APP_MODE_CONNECTED) {
		return UNIFYING_ERROR;
	}

	if (!keymap_parse(name, &stroke)) {
		return UNIFYING_ERROR;
	}

	led_blink(1);

	err = unifying_encrypted_keystroke(&ctx->state, stroke.keys, stroke.modifiers);
	if (err) {
		return err;
	}

	k_msleep(20);

	err = unifying_encrypted_keystroke(&ctx->state, release_keys, 0);
	if (err) {
		return err;
	}

	ctx->aes_counter = ctx->state.aes_counter;
	save_credentials(ctx);
	return UNIFYING_SUCCESS;
}

enum unifying_error app_do_unpair(struct app_ctx *ctx)
{
	(void)persist_clear();
	ctx->has_credentials = false;
	memset(ctx->address, 0, sizeof(ctx->address));
	memset(ctx->aes_key, 0, sizeof(ctx->aes_key));
	ctx->aes_counter = random_u32();
	unifying_state_buffers_clear(&ctx->state);
	ctx->mode = APP_MODE_IDLE;
	led_set(false);
	LOG_INF("Unpaired");
	return UNIFYING_SUCCESS;
}

void app_tick(struct app_ctx *ctx)
{
	enum unifying_error err;

	if (ctx->mode != APP_MODE_CONNECTED) {
		return;
	}

	err = unifying_tick(&ctx->state);
	if (err && err != UNIFYING_TRANSMIT_ERROR) {
		ctx->last_error = err;
	}

	/* 跳频后同步本地记录的信道（unifying_transmit 内部会改 state.channel） */
	(void)radio_esb_current_channel();
}

int main(void)
{
	struct persist_credentials cred;
	int err;

	led_init();
	LOG_INF("ProMicro Unifying starting");

	err = persist_init();
	if (err) {
		LOG_ERR("persist_init: %d", err);
	}

	err = radio_esb_init(&g_ctx.interface);
	if (err) {
		LOG_ERR("radio_esb_init: %d", err);
		return 0;
	}

	g_ctx.transmit_buffer = unifying_ring_buffer_create(TRANSMIT_BUFFER_SIZE);
	g_ctx.receive_buffer = unifying_ring_buffer_create(RECEIVE_BUFFER_SIZE);
	if (g_ctx.transmit_buffer == NULL || g_ctx.receive_buffer == NULL) {
		LOG_ERR("ring buffer alloc failed");
		return 0;
	}

	g_ctx.aes_counter = random_u32();
	g_ctx.mode = APP_MODE_IDLE;
	g_ctx.last_error = UNIFYING_SUCCESS;

	if (persist_load(&cred)) {
		memcpy(g_ctx.address, cred.address, UNIFYING_ADDRESS_LEN);
		memcpy(g_ctx.aes_key, cred.aes_key, UNIFYING_AES_BLOCK_LEN);
		g_ctx.aes_counter = cred.aes_counter;
		g_ctx.has_credentials = true;
		LOG_INF("Loaded credentials from NVS");
	} else {
		memset(g_ctx.address, 0, sizeof(g_ctx.address));
		memset(g_ctx.aes_key, 0, sizeof(g_ctx.aes_key));
		g_ctx.has_credentials = false;
		LOG_INF("No saved credentials");
	}

	state_bind(&g_ctx);

	if (g_ctx.has_credentials) {
		g_ctx.last_error = app_do_connect(&g_ctx);
		if (g_ctx.last_error) {
			LOG_WRN("Auto-connect failed: %s",
				unifying_get_error_name(g_ctx.last_error));
		}
	}

	err = usb_cli_init();
	if (err) {
		LOG_ERR("usb_cli_init: %d", err);
	}

	while (1) {
		usb_cli_poll(&g_ctx);
		app_tick(&g_ctx);
		k_msleep(1);
	}

	return 0;
}
