/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 *
 * 优联模式启动对齐 promicro_nrf52840：
 * - 有凭证：先 connect，失败则进入配对窗
 * - 无凭证：直接进入配对窗（默认 20s，每 2s 重试）
 */

#include <zmk_unifying/mode.h>
#include <zmk_unifying/persist.h>
#include <zmk_unifying/radio_esb.h>
#include <zmk_unifying/transport.h>

#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#if IS_ENABLED(CONFIG_BT)
#include <zephyr/bluetooth/bluetooth.h>
#endif

#include "unifying.h"

#include <zmk/endpoints.h>

LOG_MODULE_REGISTER(zmk_uni_transport, CONFIG_ZMK_LOG_LEVEL);

#define DEVICE_SERIAL 0xA58094B6u

static struct unifying_interface interface;
static struct unifying_ring_buffer *tx_buf;
static struct unifying_ring_buffer *rx_buf;
static struct unifying_state state;
static uint8_t address_storage[UNIFYING_ADDRESS_LEN];
static uint8_t aes_key_storage[UNIFYING_AES_BLOCK_LEN];
static struct zmk_unifying_credentials cred;
static bool has_cred;
static bool ready;
static bool connected;

static bool boot_trying;
static int64_t boot_deadline_ms;
static int64_t next_boot_attempt_ms;

static void persist_from_state(void)
{
	if (!has_cred) {
		return;
	}

	memcpy(cred.address, state.address, UNIFYING_ADDRESS_LEN);
	memcpy(cred.aes_key, state.aes_key, UNIFYING_AES_BLOCK_LEN);
	cred.aes_counter = state.aes_counter;
	(void)zmk_unifying_persist_save(&cred);
}

static void boot_pair_stop(void)
{
	boot_trying = false;
}

static void boot_pair_start(void)
{
	boot_trying = true;
	boot_deadline_ms = k_uptime_get() + CONFIG_ZMK_UNIFYING_BOOT_PAIR_WINDOW_MS;
	next_boot_attempt_ms = 0;
	LOG_INF("Boot pair window %d ms (press receiver pair button)",
		CONFIG_ZMK_UNIFYING_BOOT_PAIR_WINDOW_MS);
}

bool zmk_unifying_transport_ready(void)
{
	return ready;
}

bool zmk_unifying_transport_has_credentials(void)
{
	return has_cred;
}

bool zmk_unifying_transport_connected(void)
{
	return connected;
}

enum unifying_error zmk_unifying_transport_pair(void)
{
	enum unifying_error err;
	uint8_t id;
	uint32_t crypto;
	const char *name = CONFIG_ZMK_UNIFYING_DEVICE_NAME;
	uint8_t name_len;

	if (!ready) {
		return UNIFYING_ERROR;
	}

	id = (uint8_t)sys_rand32_get();
	crypto = sys_rand32_get();
	name_len = (uint8_t)strnlen(name, UNIFYING_MAX_NAME_LEN);

	err = unifying_pair(&state, id, (uint16_t)CONFIG_ZMK_UNIFYING_PRODUCT_ID,
			    (uint16_t)CONFIG_ZMK_UNIFYING_DEVICE_TYPE, crypto, DEVICE_SERIAL,
			    (uint16_t)CONFIG_ZMK_UNIFYING_CAPABILITIES, name, name_len);
	if (err != UNIFYING_SUCCESS) {
		LOG_WRN("pair failed: %s", unifying_get_error_name(err));
		return err;
	}

	memcpy(cred.address, state.address, UNIFYING_ADDRESS_LEN);
	memcpy(cred.aes_key, state.aes_key, UNIFYING_AES_BLOCK_LEN);
	cred.aes_counter = state.aes_counter;
	has_cred = true;
	connected = true;
	boot_pair_stop();
	(void)zmk_unifying_persist_save(&cred);
	LOG_INF("paired OK");
	return UNIFYING_SUCCESS;
}

enum unifying_error zmk_unifying_transport_unpair(void)
{
	has_cred = false;
	connected = false;
	boot_pair_stop();
	(void)zmk_unifying_persist_clear();
	LOG_INF("unpaired");
	return UNIFYING_SUCCESS;
}

enum unifying_error zmk_unifying_transport_connect(void)
{
	enum unifying_error err;

	if (!ready || !has_cred) {
		return UNIFYING_ERROR;
	}

	err = unifying_connect(&state);
	connected = (err == UNIFYING_SUCCESS);
	if (connected) {
		boot_pair_stop();
	}
	return err;
}

enum unifying_error zmk_unifying_transport_send_keys(const uint8_t keys[UNIFYING_KEYS_LEN],
						     uint8_t modifiers)
{
	enum unifying_error err;

	if (!ready) {
		return UNIFYING_ERROR;
	}

	/* 配对窗进行中不抢 RF */
	if (boot_trying) {
		return UNIFYING_ERROR;
	}

	if (!connected) {
		if (!has_cred) {
			return UNIFYING_ERROR;
		}
		err = unifying_connect(&state);
		connected = (err == UNIFYING_SUCCESS);
		if (!connected) {
			return err;
		}
	}

	err = unifying_encrypted_keystroke(&state, keys, modifiers);
	if (err == UNIFYING_SUCCESS) {
		cred.aes_counter = state.aes_counter;
	}

	return err;
}

void zmk_unifying_transport_tick(void)
{
	int64_t now;

	if (!ready || !zmk_unifying_mode_is_unifying()) {
		return;
	}

	now = k_uptime_get();

	if (boot_trying) {
		if (connected) {
			boot_pair_stop();
		} else if (now >= boot_deadline_ms) {
			LOG_WRN("Boot pair window expired");
			boot_pair_stop();
		} else if (now >= next_boot_attempt_ms) {
			next_boot_attempt_ms = now + CONFIG_ZMK_UNIFYING_BOOT_PAIR_RETRY_MS;
			LOG_INF("Boot pair attempt...");
			(void)zmk_unifying_transport_pair();
		}
		return;
	}

	if (connected) {
		(void)unifying_tick(&state);
	}
}

static void tick_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(tick_work, tick_work_handler);

static void tick_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	zmk_unifying_transport_tick();
	if (ready && zmk_unifying_mode_is_unifying()) {
		static uint8_t save_div;

		if (connected && ++save_div >= 40) {
			save_div = 0;
			persist_from_state();
		}
		k_work_schedule(&tick_work, K_MSEC(CONFIG_ZMK_UNIFYING_TICK_MS));
	}
}

int zmk_unifying_transport_init(void)
{
	enum unifying_error uerr;
	int err;

	if (!zmk_unifying_mode_is_unifying()) {
		LOG_INF("skip Unifying transport (std mode)");
		return 0;
	}

#if IS_ENABLED(CONFIG_BT)
	err = bt_disable();
	if (err && err != -EALREADY) {
		LOG_WRN("bt_disable: %d (continuing)", err);
	}
#endif

	(void)zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_NONE);

	err = radio_esb_init(&interface);
	if (err) {
		LOG_ERR("radio_esb_init: %d", err);
		return err;
	}

	tx_buf = unifying_ring_buffer_create((uint8_t)CONFIG_ZMK_UNIFYING_TX_BUFFER);
	rx_buf = unifying_ring_buffer_create((uint8_t)CONFIG_ZMK_UNIFYING_RX_BUFFER);
	if (tx_buf == NULL || rx_buf == NULL) {
		LOG_ERR("ring buffer alloc failed");
		return -ENOMEM;
	}

	has_cred = zmk_unifying_persist_load(&cred);
	if (has_cred) {
		memcpy(address_storage, cred.address, UNIFYING_ADDRESS_LEN);
		memcpy(aes_key_storage, cred.aes_key, UNIFYING_AES_BLOCK_LEN);
		LOG_INF("Loaded Unifying credentials");
	} else {
		memset(address_storage, 0, sizeof(address_storage));
		memset(aes_key_storage, 0, sizeof(aes_key_storage));
		LOG_INF("No saved credentials");
	}

	unifying_state_init(&state, &interface, tx_buf, rx_buf, address_storage, aes_key_storage,
			    has_cred ? cred.aes_counter : sys_rand32_get(),
			    UNIFYING_DEFAULT_TIMEOUT_KEYBOARD, unifying_channels[0]);

	ready = true;
	connected = false;
	boot_trying = false;

	if (has_cred) {
		LOG_INF("Boot reconnect (ch=%u)...", radio_esb_last_channel());
		uerr = unifying_connect(&state);
		connected = (uerr == UNIFYING_SUCCESS);
		if (connected) {
			LOG_INF("Boot reconnect OK");
		} else {
			LOG_WRN("Boot reconnect failed (%s) → pair window",
				unifying_get_error_name(uerr));
			boot_pair_start();
		}
	} else {
		boot_pair_start();
	}

	k_work_schedule(&tick_work, K_MSEC(CONFIG_ZMK_UNIFYING_TICK_MS));
	LOG_INF("Unifying transport ready");
	return 0;
}

static int transport_sys_init(void)
{
	return zmk_unifying_transport_init();
}

SYS_INIT(transport_sys_init, APPLICATION, 90);
