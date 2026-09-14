/*!
 * \file persist.c
 * \brief NVS 存储 Unifying 地址与 AES 密钥。
 */

#include "persist.h"

#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(persist, CONFIG_LOG_DEFAULT_LEVEL);

#define NVS_PARTITION storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

#define NVS_ID_CREDENTIALS 1
#define PERSIST_MAGIC 0x554E4901u /* 'UNI' + 1 */

struct persist_blob {
	uint32_t magic;
	uint8_t address[UNIFYING_ADDRESS_LEN];
	uint8_t aes_key[UNIFYING_AES_BLOCK_LEN];
	uint32_t aes_counter;
};

static struct nvs_fs fs;
static bool ready;

int persist_init(void)
{
	struct flash_pages_info info;
	int err;

	fs.flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(fs.flash_device)) {
		LOG_ERR("Flash device not ready");
		return -ENODEV;
	}

	fs.offset = NVS_PARTITION_OFFSET;
	err = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (err) {
		LOG_ERR("flash_get_page_info_by_offs: %d", err);
		return err;
	}

	fs.sector_size = info.size;
	fs.sector_count = 2U;

	err = nvs_mount(&fs);
	if (err) {
		LOG_ERR("nvs_mount: %d", err);
		return err;
	}

	ready = true;
	LOG_INF("NVS ready");
	return 0;
}

bool persist_load(struct persist_credentials *out)
{
	struct persist_blob blob;
	ssize_t rc;

	if (!ready || out == NULL) {
		return false;
	}

	rc = nvs_read(&fs, NVS_ID_CREDENTIALS, &blob, sizeof(blob));
	if (rc != (ssize_t)sizeof(blob)) {
		return false;
	}

	if (blob.magic != PERSIST_MAGIC) {
		return false;
	}

	memcpy(out->address, blob.address, UNIFYING_ADDRESS_LEN);
	memcpy(out->aes_key, blob.aes_key, UNIFYING_AES_BLOCK_LEN);
	out->aes_counter = blob.aes_counter;
	return true;
}

int persist_save(const struct persist_credentials *in)
{
	struct persist_blob blob;
	int err;

	if (!ready || in == NULL) {
		return -EINVAL;
	}

	memset(&blob, 0, sizeof(blob));
	blob.magic = PERSIST_MAGIC;
	memcpy(blob.address, in->address, UNIFYING_ADDRESS_LEN);
	memcpy(blob.aes_key, in->aes_key, UNIFYING_AES_BLOCK_LEN);
	blob.aes_counter = in->aes_counter;

	err = nvs_write(&fs, NVS_ID_CREDENTIALS, &blob, sizeof(blob));
	if (err < 0) {
		LOG_ERR("nvs_write: %d", err);
		return err;
	}

	return 0;
}

int persist_clear(void)
{
	int err;

	if (!ready) {
		return -EINVAL;
	}

	err = nvs_delete(&fs, NVS_ID_CREDENTIALS);
	if (err && err != -ENOENT) {
		LOG_ERR("nvs_delete: %d", err);
		return err;
	}

	return 0;
}
