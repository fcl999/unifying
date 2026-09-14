/*!
 * \file app_ctx.h
 * \brief 应用状态机与共享上下文。
 */

#ifndef APP_CTX_H
#define APP_CTX_H

#include <stdbool.h>
#include <stdint.h>

#include "persist.h"
#include "unifying.h"

#ifdef __cplusplus
extern "C" {
#endif

enum app_mode {
	APP_MODE_IDLE = 0,
	APP_MODE_PAIRING,
	APP_MODE_CONNECTED,
	APP_MODE_SLEEPING,
};

struct app_ctx {
	enum app_mode mode;
	bool has_credentials;
	enum unifying_error last_error;
	uint8_t address[UNIFYING_ADDRESS_LEN];
	uint8_t aes_key[UNIFYING_AES_BLOCK_LEN];
	uint32_t aes_counter;
	struct unifying_interface interface;
	struct unifying_ring_buffer *transmit_buffer;
	struct unifying_ring_buffer *receive_buffer;
	struct unifying_state state;
};

const char *app_mode_name(enum app_mode mode);

/*! 执行配对；成功则写 NVS 并进入 CONNECTED。 */
enum unifying_error app_do_pair(struct app_ctx *ctx);

/*! 用已有凭证连接。 */
enum unifying_error app_do_connect(struct app_ctx *ctx);

/*! 主动休眠。 */
enum unifying_error app_do_sleep(struct app_ctx *ctx);

/*! 从休眠唤醒并重连。 */
enum unifying_error app_do_wake(struct app_ctx *ctx);

/*! 发送按下再松开的按键。 */
enum unifying_error app_do_key(struct app_ctx *ctx, const char *name);

/*! 清除凭证并回到 idle。 */
enum unifying_error app_do_unpair(struct app_ctx *ctx);

/*! 已连接时调用 unifying_tick。 */
void app_tick(struct app_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_CTX_H */
