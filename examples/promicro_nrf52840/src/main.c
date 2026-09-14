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
#include "key_gpio.h"
#include "keymap.h"
#include "persist.h"
#include "radio_esb.h"
#include "usb_cli.h"
#include "unifying.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define TRANSMIT_BUFFER_SIZE 8
#define RECEIVE_BUFFER_SIZE 8
#define DEVICE_NAME "ProMicroKB"
/*! 固定无线 PID（勿用随机值：部分主机软件会把 PID 字节当成名称前缀显示成乱码）。 */
#define DEVICE_PRODUCT_ID 0x1025
#define DEVICE_SERIAL 0xA58094B6u
#define DEVICE_TYPE 0x0147
#define CAPABILITIES 0x1E40

/*! 上电未配对（或重连失败后）自动配对窗口。 */
#define BOOT_PAIR_WINDOW_MS (20 * 1000)
/*! 已连接且无按键活动后主动休眠。 */
#define IDLE_SLEEP_MS (60 * 1000)
#define BOOT_PAIR_RETRY_MS 2000
/*! 按键发送失败时跨信道重试次数（覆盖整表跳频）。 */
#define KEY_TX_CHANNEL_TRIES UNIFYING_CHANNELS_LEN

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

void app_note_activity(struct app_ctx *ctx)
{
	if (ctx != NULL) {
		ctx->last_activity_ms = k_uptime_get();
	}
}

void app_stop_boot_try(struct app_ctx *ctx)
{
	if (ctx != NULL) {
		ctx->boot_trying = false;
	}
}

void app_start_boot_pair(struct app_ctx *ctx)
{
	if (ctx == NULL) {
		return;
	}

	ctx->boot_trying = true;
	ctx->boot_deadline_ms = k_uptime_get() + BOOT_PAIR_WINDOW_MS;
	ctx->next_boot_attempt_ms = 0;
	LOG_INF("Boot pair window %d ms", BOOT_PAIR_WINDOW_MS);
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

static enum unifying_error keystroke_with_retry(struct app_ctx *ctx,
						const uint8_t keys[UNIFYING_KEYS_LEN],
						uint8_t modifiers)
{
	enum unifying_error err = UNIFYING_TRANSMIT_ERROR;
	int i;

	for (i = 0; i < KEY_TX_CHANNEL_TRIES; i++) {
		err = unifying_encrypted_keystroke(&ctx->state, keys, modifiers);
		if (err != UNIFYING_TRANSMIT_ERROR) {
			return err;
		}
		/* TRANSMIT_ERROR 时协议库已跳到下一信道，继续重试。 */
	}

	return err;
}

enum unifying_error app_do_pair(struct app_ctx *ctx)
{
	enum unifying_error err;
	uint8_t id = (uint8_t)random_u32();
	uint32_t crypto = random_u32();
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

	LOG_INF("Pairing as '%s' (len=%u) pid=0x%04x", name, name_length, DEVICE_PRODUCT_ID);

	err = unifying_pair(&ctx->state,
			    id,
			    DEVICE_PRODUCT_ID,
			    DEVICE_TYPE,
			    crypto,
			    DEVICE_SERIAL,
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
	app_note_activity(ctx);
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
	app_note_activity(ctx);
	LOG_INF("Connected");
	return UNIFYING_SUCCESS;
}

enum unifying_error app_do_sleep(struct app_ctx *ctx)
{
	app_stop_boot_try(ctx);
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

	app_stop_boot_try(ctx);

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
	bool in_boot_pair;

	/* 未连接且未配对：忽略（不自动 pair） */
	if (ctx->mode != APP_MODE_CONNECTED && !ctx->has_credentials) {
		return UNIFYING_ERROR;
	}

	/*
	 * 未连接但已有凭证：尝试重连一次。
	 * 若正处于上电配对窗（重连失败后的 20s pair），按键表示用户想用键盘：
	 * 重连成功则停止配对并发键；失败则保持配对窗继续。
	 * 注意：不用 app_do_wake()，避免失败时也清掉 boot_trying。
	 */
	if (ctx->mode != APP_MODE_CONNECTED) {
		in_boot_pair = ctx->boot_trying;
		if (in_boot_pair) {
			LOG_INF("Key during pair window → reconnect once");
		}

		err = app_do_connect(ctx);
		if (err) {
			if (in_boot_pair) {
				LOG_WRN("Key reconnect failed (%s), continue pairing",
					unifying_get_error_name(err));
			}
			return err;
		}

		app_stop_boot_try(ctx);
		app_note_activity(ctx);
	}

	if (ctx->mode != APP_MODE_CONNECTED) {
		return UNIFYING_ERROR;
	}

	if (!keymap_parse(name, &stroke)) {
		return UNIFYING_ERROR;
	}

	led_blink(1);

	err = keystroke_with_retry(ctx, stroke.keys, stroke.modifiers);
	if (err) {
		return err;
	}

	ctx->aes_counter = ctx->state.aes_counter;
	save_credentials(ctx);

	k_msleep(20);

	err = keystroke_with_retry(ctx, release_keys, 0);
	if (err) {
		return err;
	}

	ctx->aes_counter = ctx->state.aes_counter;
	save_credentials(ctx);
	app_note_activity(ctx);
	return UNIFYING_SUCCESS;
}

enum unifying_error app_do_unpair(struct app_ctx *ctx)
{
	app_stop_boot_try(ctx);
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

void app_power_manage(struct app_ctx *ctx)
{
	int64_t now = k_uptime_get();

	/* 仅处理上电配对窗口；重连在 main 里只做一次 */
	if (ctx->boot_trying) {
		if (ctx->mode == APP_MODE_CONNECTED) {
			app_stop_boot_try(ctx);
			app_note_activity(ctx);
			return;
		}

		if (now >= ctx->boot_deadline_ms) {
			LOG_WRN("Boot pair window expired → sleep");
			app_stop_boot_try(ctx);
			(void)app_do_sleep(ctx);
			return;
		}

		if (now < ctx->next_boot_attempt_ms) {
			return;
		}

		ctx->next_boot_attempt_ms = now + BOOT_PAIR_RETRY_MS;
		LOG_INF("Boot pair attempt (receiver must be in pairing mode)...");
		ctx->last_error = app_do_pair(ctx);

		if (ctx->mode == APP_MODE_CONNECTED) {
			app_stop_boot_try(ctx);
			app_note_activity(ctx);
		} else if (ctx->last_error) {
			LOG_WRN("Boot pair failed: %s",
				unifying_get_error_name(ctx->last_error));
		}
		return;
	}

	if (ctx->mode == APP_MODE_CONNECTED &&
	    (now - ctx->last_activity_ms) >= IDLE_SLEEP_MS) {
		LOG_INF("No key activity for %d ms → sleep", IDLE_SLEEP_MS);
		(void)app_do_sleep(ctx);
	}
}

int main(void)
{
	struct persist_credentials cred;
	int err;

	led_init();
	LOG_INF("ProMicro Unifying starting");

	err = usb_cli_init();
	if (err) {
		LOG_ERR("usb_cli_init: %d", err);
	}

	err = persist_init();
	if (err) {
		LOG_ERR("persist_init: %d", err);
	}

	err = radio_esb_init(&g_ctx.interface);
	if (err) {
		LOG_ERR("radio_esb_init: %d", err);
	}

	(void)key_gpio_init();

	g_ctx.transmit_buffer = unifying_ring_buffer_create(TRANSMIT_BUFFER_SIZE);
	g_ctx.receive_buffer = unifying_ring_buffer_create(RECEIVE_BUFFER_SIZE);
	if (g_ctx.transmit_buffer == NULL || g_ctx.receive_buffer == NULL) {
		LOG_ERR("ring buffer alloc failed");
		return 0;
	}

	g_ctx.aes_counter = random_u32();
	g_ctx.mode = APP_MODE_IDLE;
	g_ctx.last_error = UNIFYING_SUCCESS;
	g_ctx.last_activity_ms = k_uptime_get();

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

	/*
	 * 上电策略：
	 * - 已配对：先 reconnect 一次；失败则立即进入 20s pair 窗
	 * - 未配对：直接进入 20s pair 窗
	 */
	g_ctx.boot_trying = false;
	if (g_ctx.has_credentials) {
		LOG_INF("Boot reconnect once...");
		g_ctx.last_error = app_do_connect(&g_ctx);
		if (g_ctx.mode == APP_MODE_CONNECTED) {
			app_note_activity(&g_ctx);
			LOG_INF("Boot reconnect OK → idle sleep timer armed");
		} else {
			LOG_WRN("Boot reconnect failed (%s) → start pair window",
				unifying_get_error_name(g_ctx.last_error));
			app_start_boot_pair(&g_ctx);
		}
	} else {
		app_start_boot_pair(&g_ctx);
	}

	while (1) {
		usb_cli_poll(&g_ctx);
		key_gpio_poll(&g_ctx);
		app_power_manage(&g_ctx);
		app_tick(&g_ctx);
		k_msleep(1);
	}

	return 0;
}
