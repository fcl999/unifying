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

/*!
 * 初始化 CDC 控制台 CLI（等待 DTR 后打印 banner）。
 *
 * \return 0 成功。
 */
int usb_cli_init(void);

/*!
 * 轮询读取一行命令并执行。应在主循环中调用。
 */
void usb_cli_poll(struct app_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* USB_CLI_H */
