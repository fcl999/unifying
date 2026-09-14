/*!
 * \file radio_esb.h
 * \brief Nordic ESB 适配层，实现 unifying_interface 射频回调。
 */

#ifndef RADIO_ESB_H
#define RADIO_ESB_H

#include <stdbool.h>
#include <stdint.h>

#include "unifying.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * 初始化 ESB PTX，并填充 unifying_interface 中的射频相关回调。
 * encrypt 回调保持为 NULL（使用库内置 tiny-AES）。
 *
 * \return 0 成功，负值失败。
 */
int radio_esb_init(struct unifying_interface *interface);

/*!
 * 关闭 RADIO（主动休眠）。
 */
void radio_esb_sleep(void);

/*!
 * 从休眠恢复 RADIO，并还原最近一次地址与信道。
 *
 * \return 0 成功，负值失败。
 */
int radio_esb_wake(void);

/*!
 * 当前是否处于休眠（RADIO 关闭）。
 */
bool radio_esb_is_sleeping(void);

/*!
 * 最近一次设置的 RF 信道（Unifying 信道号）。
 */
uint8_t radio_esb_current_channel(void);

#ifdef __cplusplus
}
#endif

#endif /* RADIO_ESB_H */
