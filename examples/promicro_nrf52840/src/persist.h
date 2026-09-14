/*!
 * \file persist.h
 * \brief 使用 NVS 保存 / 读取 Unifying 配对凭证。
 */

#ifndef PERSIST_H
#define PERSIST_H

#include <stdbool.h>
#include <stdint.h>

#include "unifying_const.h"

#ifdef __cplusplus
extern "C" {
#endif

struct persist_credentials {
	uint8_t address[UNIFYING_ADDRESS_LEN];
	uint8_t aes_key[UNIFYING_AES_BLOCK_LEN];
	uint32_t aes_counter;
};

/*!
 * 初始化 NVS。
 *
 * \return 0 成功，负值失败。
 */
int persist_init(void);

/*!
 * 从 NVS 加载凭证。
 *
 * \return true 若存在有效凭证。
 */
bool persist_load(struct persist_credentials *out);

/*!
 * 保存凭证到 NVS。
 *
 * \return 0 成功，负值失败。
 */
int persist_save(const struct persist_credentials *in);

/*!
 * 清除已保存的凭证。
 *
 * \return 0 成功，负值失败。
 */
int persist_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* PERSIST_H */
