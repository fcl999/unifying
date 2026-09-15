/*!
 * \file key_gpio.h
 * \brief GPIOTE 按键：休眠唤醒并异步发键。
 */

#ifndef KEY_GPIO_H
#define KEY_GPIO_H

#include "app_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

int key_gpio_init(void);

/*! 消费 IRQ pending，入队 app_key_request。 */
void key_gpio_service(struct app_ctx *ctx);

/*! 休眠时阻塞等待 GPIOTE（或任意 give）。 */
void key_gpio_wait_wake(void);

/*! 清空唤醒信号，避免假醒。 */
void key_gpio_clear_wake(void);

#ifdef __cplusplus
}
#endif

#endif /* KEY_GPIO_H */
