/*!
 * \file usb_cli.h
 * \brief USB CDC 文本命令行。
 */

#ifndef USB_CLI_H
#define USB_CLI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct app_ctx;

int usb_cli_init(void);

/*! 休眠时禁用 USB。 */
int usb_cli_suspend(void);

/*! 唤醒后重新使能 USB（需重新打开串口）。 */
int usb_cli_resume(void);

bool usb_cli_is_suspended(void);

void usb_cli_poll(struct app_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* USB_CLI_H */
