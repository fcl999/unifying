/*!
 * \file key_gpio.h
 * \brief 板载/外接按键：休眠唤醒并发送按键。
 */

#ifndef KEY_GPIO_H
#define KEY_GPIO_H

#include "app_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! 初始化 sw0（无别名时安全跳过）。 */
int key_gpio_init(void);

/*!
 * 轮询按键边沿。
 * 按下时：若休眠则唤醒重连，再发送配置的键名（默认 "a"）。
 */
void key_gpio_poll(struct app_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KEY_GPIO_H */
