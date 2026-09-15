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

/*! 异步 RF / 发键状态机阶段。 */
enum app_rf_phase {
	APP_RF_IDLE = 0,
	APP_RF_WAKE_USB_RADIO,
	APP_RF_CONNECT_TRY,
	APP_RF_SEND_PRESS,
	APP_RF_WAIT_RELEASE,
	APP_RF_SEND_RELEASE,
};

#define APP_KEY_NAME_MAX 32

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

	/*! 上电配对窗口进行中。 */
	bool boot_trying;
	int64_t boot_deadline_ms;
	int64_t next_boot_attempt_ms;
	/*! 上电异步重连进行中。 */
	bool boot_connect_pending;
	/*! 最近一次有效按键活动（用于空闲休眠）。 */
	int64_t last_activity_ms;

	/*! 异步 RF 状态。 */
	enum app_rf_phase rf_phase;
	uint8_t connect_attempts;
	bool key_pending;
	bool keep_boot_pair_on_fail;
	char key_name[APP_KEY_NAME_MAX];
	uint8_t pending_keys[UNIFYING_KEYS_LEN];
	uint8_t pending_modifiers;
	uint8_t press_attempts;
	int64_t release_at_ms;
	bool usb_suspended;
};

/*! 打开上电 20s 自动配对窗口。 */
void app_start_boot_pair(struct app_ctx *ctx);

const char *app_mode_name(enum app_mode mode);

void app_note_activity(struct app_ctx *ctx);
void app_stop_boot_try(struct app_ctx *ctx);
void app_power_manage(struct app_ctx *ctx);

enum unifying_error app_do_pair(struct app_ctx *ctx);
enum unifying_error app_do_sleep(struct app_ctx *ctx);
enum unifying_error app_do_wake(struct app_ctx *ctx);
enum unifying_error app_do_unpair(struct app_ctx *ctx);

/*! 异步：请求发送按键（入队后立即返回）。 */
enum unifying_error app_key_request(struct app_ctx *ctx, const char *name);

/*! 异步：仅请求重连（无按键）。 */
enum unifying_error app_connect_request(struct app_ctx *ctx);

/*! 每圈最多推进一步 RF/发键状态机。 */
void app_rf_poll(struct app_ctx *ctx);

/*! RF 状态机是否忙（勿在忙时进休眠/配对）。 */
bool app_rf_busy(const struct app_ctx *ctx);

void app_tick(struct app_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_CTX_H */
