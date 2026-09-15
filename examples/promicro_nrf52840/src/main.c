/*!
 * \file main.c
 * \brief Pro Micro nRF52840 Unifying：异步唤醒发键 + USB/RADIO 休眠。
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
#define DEVICE_PRODUCT_ID 0x1025
#define DEVICE_SERIAL 0xA58094B6u
#define DEVICE_TYPE 0x0147
#define CAPABILITIES 0x1E40

#define BOOT_PAIR_WINDOW_MS (20 * 1000)
#define IDLE_SLEEP_MS (60 * 1000)
#define BOOT_PAIR_RETRY_MS 2000
#define KEY_RELEASE_GAP_MS 20

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

bool app_rf_busy(const struct app_ctx *ctx)
{
	return ctx != NULL && ctx->rf_phase != APP_RF_IDLE;
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

static void sync_counter_from_state(struct app_ctx *ctx)
{
	ctx->aes_counter = ctx->state.aes_counter;
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
			    radio_esb_last_channel());
}

static enum unifying_error rf_prepare_radio(struct app_ctx *ctx)
{
	uint8_t ch;

	if (ctx->usb_suspended) {
		if (usb_cli_resume() != 0) {
			return UNIFYING_ERROR;
		}
		ctx->usb_suspended = false;
	}

	if (radio_esb_is_sleeping()) {
		if (radio_esb_wake() != 0) {
			return UNIFYING_ERROR;
		}
	}

	if (ctx->interface.set_address(ctx->address) != 0) {
		return UNIFYING_SET_ADDRESS_ERROR;
	}

	ch = radio_esb_last_channel();
	if (ctx->interface.set_channel(ch) != 0) {
		return UNIFYING_SET_CHANNEL_ERROR;
	}

	ctx->state.channel = ch;
	unifying_state_buffers_clear(&ctx->state);
	ctx->state.next_transmit = 0;
	ctx->state.previous_transmit = 0;

	LOG_INF("RF start on channel %u (last)", ch);
	return unifying_connect_begin(&ctx->state);
}

static void rf_finish_fail(struct app_ctx *ctx, enum unifying_error err)
{
	ctx->last_error = err;
	ctx->rf_phase = APP_RF_IDLE;
	ctx->key_pending = false;

	if (ctx->boot_connect_pending) {
		ctx->boot_connect_pending = false;
		LOG_WRN("Boot reconnect failed (%s) → pair window",
			unifying_get_error_name(err));
		app_start_boot_pair(ctx);
		return;
	}

	if (ctx->keep_boot_pair_on_fail && ctx->boot_trying) {
		LOG_WRN("Key reconnect failed (%s), continue pairing",
			unifying_get_error_name(err));
		ctx->keep_boot_pair_on_fail = false;
		ctx->mode = APP_MODE_IDLE;
		led_set(false);
		return;
	}

	ctx->keep_boot_pair_on_fail = false;
	ctx->mode = APP_MODE_IDLE;
	led_set(false);
}

static void rf_finish_connected(struct app_ctx *ctx)
{
	ctx->mode = APP_MODE_CONNECTED;
	led_set(true);
	app_note_activity(ctx);
	app_stop_boot_try(ctx);
	ctx->boot_connect_pending = false;
	ctx->keep_boot_pair_on_fail = false;
	ctx->rf_phase = APP_RF_IDLE;
	LOG_INF("Connected (ch=%u)", radio_esb_last_channel());
}

static enum unifying_error rf_start(struct app_ctx *ctx, bool with_key, const char *name)
{
	struct keymap_stroke stroke;

	if (app_rf_busy(ctx)) {
		return UNIFYING_ERROR;
	}

	if (!ctx->has_credentials && ctx->mode != APP_MODE_CONNECTED) {
		return UNIFYING_ERROR;
	}

	ctx->key_pending = with_key;
	ctx->keep_boot_pair_on_fail = ctx->boot_trying;
	ctx->connect_attempts = 0;
	ctx->press_attempts = 0;
	ctx->release_at_ms = 0;

	if (with_key) {
		if (name == NULL || name[0] == '\0') {
			return UNIFYING_ERROR;
		}
		if (!keymap_parse(name, &stroke)) {
			return UNIFYING_ERROR;
		}
		strncpy(ctx->key_name, name, sizeof(ctx->key_name) - 1);
		ctx->key_name[sizeof(ctx->key_name) - 1] = '\0';
		memcpy(ctx->pending_keys, stroke.keys, UNIFYING_KEYS_LEN);
		ctx->pending_modifiers = stroke.modifiers;
	} else {
		ctx->key_name[0] = '\0';
	}

	if (ctx->mode == APP_MODE_CONNECTED && with_key) {
		ctx->rf_phase = APP_RF_SEND_PRESS;
		return UNIFYING_SUCCESS;
	}

	ctx->rf_phase = APP_RF_WAKE_USB_RADIO;
	return UNIFYING_SUCCESS;
}

enum unifying_error app_key_request(struct app_ctx *ctx, const char *name)
{
	return rf_start(ctx, true, name);
}

enum unifying_error app_connect_request(struct app_ctx *ctx)
{
	if (!ctx->has_credentials) {
		return UNIFYING_ERROR;
	}

	return rf_start(ctx, false, NULL);
}

void app_rf_poll(struct app_ctx *ctx)
{
	enum unifying_error err;
	uint8_t release_keys[UNIFYING_KEYS_LEN] = {0};

	if (ctx == NULL || ctx->rf_phase == APP_RF_IDLE) {
		return;
	}

	switch (ctx->rf_phase) {
	case APP_RF_WAKE_USB_RADIO:
		err = rf_prepare_radio(ctx);
		if (err) {
			rf_finish_fail(ctx, err);
			return;
		}
		ctx->rf_phase = APP_RF_CONNECT_TRY;
		return;

	case APP_RF_CONNECT_TRY:
		err = unifying_loop(&ctx->state, true, true, false);
		if (!err) {
			if (ctx->key_pending) {
				ctx->rf_phase = APP_RF_SEND_PRESS;
			} else {
				rf_finish_connected(ctx);
			}
			return;
		}

		ctx->connect_attempts++;
		ctx->last_error = err;
		if (ctx->connect_attempts >= UNIFYING_CHANNELS_LEN) {
			unifying_state_buffers_clear(&ctx->state);
			rf_finish_fail(ctx, err);
			return;
		}
		/* 失败时协议库已 hop，wake 仍在队列，下圈再试 */
		return;

	case APP_RF_SEND_PRESS:
		led_set(true);
		err = unifying_encrypted_keystroke(&ctx->state,
						   ctx->pending_keys,
						   ctx->pending_modifiers);
		if (err == UNIFYING_TRANSMIT_ERROR) {
			ctx->press_attempts++;
			if (ctx->press_attempts >= UNIFYING_CHANNELS_LEN) {
				rf_finish_fail(ctx, err);
			}
			return;
		}
		if (err) {
			rf_finish_fail(ctx, err);
			return;
		}
		sync_counter_from_state(ctx);
		ctx->press_attempts = 0;
		ctx->release_at_ms = k_uptime_get() + KEY_RELEASE_GAP_MS;
		ctx->rf_phase = APP_RF_WAIT_RELEASE;
		ctx->mode = APP_MODE_CONNECTED;
		app_stop_boot_try(ctx);
		ctx->boot_connect_pending = false;
		ctx->keep_boot_pair_on_fail = false;
		return;

	case APP_RF_WAIT_RELEASE:
		if (k_uptime_get() < ctx->release_at_ms) {
			return;
		}
		ctx->rf_phase = APP_RF_SEND_RELEASE;
		ctx->press_attempts = 0;
		return;

	case APP_RF_SEND_RELEASE:
		err = unifying_encrypted_keystroke(&ctx->state, release_keys, 0);
		if (err == UNIFYING_TRANSMIT_ERROR) {
			ctx->press_attempts++;
			if (ctx->press_attempts >= UNIFYING_CHANNELS_LEN) {
				ctx->last_error = err;
				ctx->key_pending = false;
				app_note_activity(ctx);
				ctx->rf_phase = APP_RF_IDLE;
			}
			return;
		}
		if (err) {
			/* 按下已成功；松开失败仍算已连接 */
			ctx->last_error = err;
		} else {
			sync_counter_from_state(ctx);
			ctx->last_error = UNIFYING_SUCCESS;
		}
		ctx->key_pending = false;
		app_note_activity(ctx);
		ctx->rf_phase = APP_RF_IDLE;
		led_set(true);
		return;

	default:
		ctx->rf_phase = APP_RF_IDLE;
		return;
	}
}

enum unifying_error app_do_pair(struct app_ctx *ctx)
{
	enum unifying_error err;
	uint8_t id = (uint8_t)random_u32();
	uint32_t crypto = random_u32();
	const char *name = DEVICE_NAME;
	uint8_t name_length = (uint8_t)strlen(name);

	if (app_rf_busy(ctx)) {
		return UNIFYING_ERROR;
	}

	if (ctx->usb_suspended) {
		(void)usb_cli_resume();
		ctx->usb_suspended = false;
	}

	if (radio_esb_is_sleeping()) {
		if (radio_esb_wake() != 0) {
			return UNIFYING_ERROR;
		}
	}

	ctx->mode = APP_MODE_PAIRING;
	led_set(true);
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

	sync_counter_from_state(ctx);
	save_credentials(ctx);
	ctx->mode = APP_MODE_CONNECTED;
	led_set(true);
	app_note_activity(ctx);
	LOG_INF("Paired OK ch=%u", radio_esb_last_channel());
	return UNIFYING_SUCCESS;
}

enum unifying_error app_do_sleep(struct app_ctx *ctx)
{
	app_stop_boot_try(ctx);
	ctx->boot_connect_pending = false;
	ctx->rf_phase = APP_RF_IDLE;
	ctx->key_pending = false;

	/* 休眠前刷一次 counter（平时只改 RAM） */
	if (ctx->has_credentials) {
		sync_counter_from_state(ctx);
		save_credentials(ctx);
	}

	unifying_state_buffers_clear(&ctx->state);
	radio_esb_sleep();

	if (!ctx->usb_suspended) {
		(void)usb_cli_suspend();
		ctx->usb_suspended = true;
	}

	key_gpio_clear_wake();

	ctx->mode = APP_MODE_SLEEPING;
	led_set(false);
	LOG_INF("Sleeping (RADIO+USB off, last_ch=%u)", radio_esb_last_channel());
	return UNIFYING_SUCCESS;
}

enum unifying_error app_do_wake(struct app_ctx *ctx)
{
	app_stop_boot_try(ctx);
	return app_connect_request(ctx);
}

enum unifying_error app_do_unpair(struct app_ctx *ctx)
{
	app_stop_boot_try(ctx);
	ctx->boot_connect_pending = false;
	ctx->rf_phase = APP_RF_IDLE;
	ctx->key_pending = false;
	(void)persist_clear();
	ctx->has_credentials = false;
	memset(ctx->address, 0, sizeof(ctx->address));
	memset(ctx->aes_key, 0, sizeof(ctx->aes_key));
	ctx->aes_counter = random_u32();
	unifying_state_buffers_clear(&ctx->state);

	if (ctx->usb_suspended) {
		(void)usb_cli_resume();
		ctx->usb_suspended = false;
	}

	ctx->mode = APP_MODE_IDLE;
	led_set(false);
	LOG_INF("Unpaired");
	return UNIFYING_SUCCESS;
}

void app_tick(struct app_ctx *ctx)
{
	enum unifying_error err;

	if (ctx->mode != APP_MODE_CONNECTED || app_rf_busy(ctx)) {
		return;
	}

	err = unifying_tick(&ctx->state);
	if (err && err != UNIFYING_TRANSMIT_ERROR) {
		ctx->last_error = err;
	}

	(void)radio_esb_current_channel();
}

void app_power_manage(struct app_ctx *ctx)
{
	int64_t now = k_uptime_get();

	if (app_rf_busy(ctx)) {
		return;
	}

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
	g_ctx.rf_phase = APP_RF_IDLE;
	g_ctx.usb_suspended = false;
	g_ctx.boot_connect_pending = false;

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

	g_ctx.boot_trying = false;
	if (g_ctx.has_credentials) {
		LOG_INF("Boot reconnect (async, last_ch=%u)...", radio_esb_last_channel());
		g_ctx.boot_connect_pending = true;
		g_ctx.last_error = app_connect_request(&g_ctx);
		if (g_ctx.last_error) {
			g_ctx.boot_connect_pending = false;
			app_start_boot_pair(&g_ctx);
		}
	} else {
		app_start_boot_pair(&g_ctx);
	}

	while (1) {
		if (g_ctx.mode == APP_MODE_SLEEPING && !app_rf_busy(&g_ctx)) {
			key_gpio_wait_wake();
		}

		usb_cli_poll(&g_ctx);
		key_gpio_service(&g_ctx);
		app_rf_poll(&g_ctx);
		app_power_manage(&g_ctx);
		app_tick(&g_ctx);

		if (g_ctx.mode != APP_MODE_SLEEPING || app_rf_busy(&g_ctx)) {
			k_msleep(1);
		}
	}

	return 0;
}
