/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zmk_unifying/mode.h>
#include <zmk_unifying/persist.h>

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(zmk_uni_persist, CONFIG_ZMK_LOG_LEVEL);

#define SETTINGS_ROOT "zmk_uni"
#define SETTINGS_CRED "zmk_uni/cred"
#define SETTINGS_MODE "zmk_uni/mode"
#define SETTINGS_PENDING "zmk_uni/pending"

#define PERSIST_MAGIC 0x554E4901u

struct persist_blob {
	uint32_t magic;
	uint8_t address[UNIFYING_ADDRESS_LEN];
	uint8_t aes_key[UNIFYING_AES_BLOCK_LEN];
	uint32_t aes_counter;
} __packed;

static struct persist_blob cached_cred;
static bool have_cred;
static uint8_t cached_mode = ZMK_UNIFYING_MODE_STD;
static bool mode_loaded;
static uint8_t cached_pending = ZMK_UNI_PENDING_NONE;
static bool pending_loaded;
static bool ready;

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	int rc;

	if (settings_name_steq(name, "cred", &next) && !next) {
		if (len != sizeof(cached_cred)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &cached_cred, sizeof(cached_cred));
		if (rc >= 0 && cached_cred.magic == PERSIST_MAGIC) {
			have_cred = true;
		}
		return 0;
	}

	if (settings_name_steq(name, "mode", &next) && !next) {
		if (len != sizeof(cached_mode)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &cached_mode, sizeof(cached_mode));
		if (rc >= 0) {
			mode_loaded = true;
		}
		return 0;
	}

	if (settings_name_steq(name, "pending", &next) && !next) {
		if (len != sizeof(cached_pending)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &cached_pending, sizeof(cached_pending));
		if (rc >= 0) {
			pending_loaded = true;
		}
		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(zmk_uni, SETTINGS_ROOT, NULL, settings_set, NULL, NULL);

int zmk_unifying_persist_init(void)
{
	int err = settings_subsys_init();

	if (err && err != -EALREADY) {
		LOG_ERR("settings_subsys_init: %d", err);
		return err;
	}

	err = settings_load_subtree(SETTINGS_ROOT);
	if (err) {
		LOG_WRN("settings_load_subtree: %d", err);
	}

	ready = true;
	return 0;
}

bool zmk_unifying_persist_load(struct zmk_unifying_credentials *out)
{
	if (!ready || out == NULL || !have_cred) {
		return false;
	}

	memcpy(out->address, cached_cred.address, UNIFYING_ADDRESS_LEN);
	memcpy(out->aes_key, cached_cred.aes_key, UNIFYING_AES_BLOCK_LEN);
	out->aes_counter = cached_cred.aes_counter;
	return true;
}

int zmk_unifying_persist_save(const struct zmk_unifying_credentials *in)
{
	int err;

	if (!ready || in == NULL) {
		return -EINVAL;
	}

	memset(&cached_cred, 0, sizeof(cached_cred));
	cached_cred.magic = PERSIST_MAGIC;
	memcpy(cached_cred.address, in->address, UNIFYING_ADDRESS_LEN);
	memcpy(cached_cred.aes_key, in->aes_key, UNIFYING_AES_BLOCK_LEN);
	cached_cred.aes_counter = in->aes_counter;
	have_cred = true;

	err = settings_save_one(SETTINGS_CRED, &cached_cred, sizeof(cached_cred));
	if (err) {
		LOG_ERR("save cred: %d", err);
		return err;
	}

	return 0;
}

int zmk_unifying_persist_clear(void)
{
	int err;

	if (!ready) {
		return -EINVAL;
	}

	have_cred = false;
	memset(&cached_cred, 0, sizeof(cached_cred));
	err = settings_delete(SETTINGS_CRED);
	if (err && err != -ENOENT) {
		return err;
	}

	return 0;
}

int zmk_unifying_persist_load_mode(uint8_t *mode)
{
	if (!ready || mode == NULL) {
		return -EINVAL;
	}

	*mode = mode_loaded ? cached_mode : ZMK_UNIFYING_MODE_STD;
	return 0;
}

int zmk_unifying_persist_save_mode(uint8_t mode)
{
	int err;

	if (!ready) {
		return -EINVAL;
	}

	cached_mode = mode;
	mode_loaded = true;
	err = settings_save_one(SETTINGS_MODE, &cached_mode, sizeof(cached_mode));
	if (err) {
		LOG_ERR("save mode: %d", err);
		return err;
	}

	return 0;
}

int zmk_unifying_persist_load_pending(uint8_t *pending)
{
	if (!ready || pending == NULL) {
		return -EINVAL;
	}

	*pending = pending_loaded ? cached_pending : ZMK_UNI_PENDING_NONE;
	return 0;
}

int zmk_unifying_persist_save_pending(uint8_t pending)
{
	int err;

	if (!ready) {
		return -EINVAL;
	}

	cached_pending = pending;
	pending_loaded = true;
	err = settings_save_one(SETTINGS_PENDING, &cached_pending, sizeof(cached_pending));
	if (err) {
		LOG_ERR("save pending: %d", err);
		return err;
	}

	return 0;
}
