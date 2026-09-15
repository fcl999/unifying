/*!
 * \file usb_cli.c
 * \brief USB CDC 文本命令处理。
 */

#include "usb_cli.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usb_device.h>

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
#include <zephyr/drivers/usb/udc.h>
#endif

#include "app_ctx.h"
#include "keymap.h"
#include "radio_esb.h"
#include "unifying_error.h"

LOG_MODULE_REGISTER(usb_cli, CONFIG_LOG_DEFAULT_LEVEL);

#define LINE_MAX 64
#define RX_RING_SIZE 256

static const struct device *cdc_dev;
#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
static const struct device *udc_dev;
#endif
static struct ring_buf rx_ring;
static uint8_t rx_ring_data[RX_RING_SIZE];
static char line_buf[LINE_MAX];
static size_t line_len;
static bool irq_enabled;
static bool banner_shown;
static bool usb_suspended;

static void print_help(void)
{
	printk("Commands:\n");
	printk("  help              show this help\n");
	printk("  status            show connection state\n");
	printk("  pair              pair with Unifying receiver\n");
	printk("  sleep             RADIO+USB sleep (GPIOTE wake)\n");
	printk("  wake              async wake-up and reconnect\n");
	printk("  key <name>        queue key press+release\n");
	printk("  unpair            erase credentials\n");
	keymap_print_help();
}

static void print_status(struct app_ctx *ctx)
{
	printk("mode=%s paired=%s channel=%u last_err=%s (%d)\n",
	       app_mode_name(ctx->mode),
	       ctx->has_credentials ? "yes" : "no",
	       radio_esb_current_channel(),
	       unifying_get_error_name(ctx->last_error),
	       (int)ctx->last_error);
}

static void reply_ok(void)
{
	printk("OK\n");
}

static void reply_err(const char *msg, enum unifying_error err)
{
	if (err != UNIFYING_SUCCESS) {
		printk("ERR %s (%s)\n", msg, unifying_get_error_name(err));
	} else {
		printk("ERR %s\n", msg);
	}
}

static void trim_inplace(char *s)
{
	size_t n;
	char *start = s;

	while (*start != '\0' && isspace((unsigned char)*start)) {
		start++;
	}

	if (start != s) {
		memmove(s, start, strlen(start) + 1);
	}

	n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1])) {
		s[n - 1] = '\0';
		n--;
	}
}

static void handle_line(struct app_ctx *ctx, char *line)
{
	enum unifying_error err;

	trim_inplace(line);
	if (line[0] == '\0') {
		return;
	}

	if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
		print_help();
		reply_ok();
		return;
	}

	if (strcmp(line, "status") == 0) {
		print_status(ctx);
		reply_ok();
		return;
	}

	if (strcmp(line, "pair") == 0) {
		app_stop_boot_try(ctx);
		printk("Pairing... put receiver in pairing mode first.\n");
		err = app_do_pair(ctx);
		ctx->last_error = err;
		if (err) {
			reply_err("pair_failed", err);
		} else {
			reply_ok();
		}
		return;
	}

	if (strcmp(line, "sleep") == 0) {
		err = app_do_sleep(ctx);
		ctx->last_error = err;
		if (err) {
			reply_err("sleep_failed", err);
		} else {
			reply_ok();
		}
		return;
	}

	if (strcmp(line, "wake") == 0) {
		err = app_do_wake(ctx);
		ctx->last_error = err;
		if (err) {
			reply_err("wake_failed", err);
		} else {
			printk("Wake queued\n");
			reply_ok();
		}
		return;
	}

	if (strncmp(line, "key ", 4) == 0) {
		err = app_key_request(ctx, line + 4);
		ctx->last_error = err;
		if (err == UNIFYING_ERROR) {
			if (!ctx->has_credentials && ctx->mode != APP_MODE_CONNECTED) {
				reply_err("not_paired", UNIFYING_SUCCESS);
			} else if (app_rf_busy(ctx)) {
				reply_err("busy", UNIFYING_SUCCESS);
			} else {
				reply_err("unknown_key", UNIFYING_SUCCESS);
			}
		} else if (err) {
			reply_err("key_failed", err);
		} else {
			reply_ok();
		}
		return;
	}

	if (strcmp(line, "unpair") == 0) {
		err = app_do_unpair(ctx);
		ctx->last_error = err;
		if (err) {
			reply_err("unpair_failed", err);
		} else {
			reply_ok();
		}
		return;
	}

	reply_err("unknown_cmd", UNIFYING_SUCCESS);
}

static void cdc_interrupt_handler(const struct device *dev, void *user_data)
{
	uint8_t tmp[64];
	int rx;

	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (!uart_irq_rx_ready(dev)) {
			continue;
		}

		rx = uart_fifo_read(dev, tmp, sizeof(tmp));
		if (rx > 0) {
			ring_buf_put(&rx_ring, tmp, rx);
		}
	}
}

static void try_show_banner(struct app_ctx *ctx)
{
	uint32_t dtr = 0;

	if (banner_shown || cdc_dev == NULL) {
		return;
	}

	(void)uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);
	if (!dtr) {
		return;
	}

	(void)uart_line_ctrl_set(cdc_dev, UART_LINE_CTRL_DCD, 1);
	(void)uart_line_ctrl_set(cdc_dev, UART_LINE_CTRL_DSR, 1);

	if (!irq_enabled) {
		uart_irq_callback_set(cdc_dev, cdc_interrupt_handler);
		uart_irq_rx_enable(cdc_dev);
		irq_enabled = true;
	}

	printk("\n=== ProMicro Unifying CLI ===\n");
	printk("Type 'help' for commands.\n");
	printk("Boot status: mode=%s paired=%s last_err=%s\n",
	       app_mode_name(ctx->mode),
	       ctx->has_credentials ? "yes" : "no",
	       unifying_get_error_name(ctx->last_error));
	banner_shown = true;
}

int usb_cli_init(void)
{
	int err;

	cdc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(cdc_dev)) {
		LOG_ERR("CDC ACM not ready");
		return -ENODEV;
	}

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
	udc_dev = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));
	if (!device_is_ready(udc_dev)) {
		LOG_WRN("zephyr_udc0 not ready — USB suspend may fail");
		udc_dev = NULL;
	}
#endif

	ring_buf_init(&rx_ring, sizeof(rx_ring_data), rx_ring_data);
	usb_suspended = false;
	banner_shown = false;
	irq_enabled = false;

#if defined(CONFIG_USB_DEVICE_STACK) && !defined(CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT)
	err = usb_enable(NULL);
	if (err && err != -EALREADY) {
		LOG_ERR("usb_enable: %d", err);
		return err;
	}
#else
	ARG_UNUSED(err);
#endif

	LOG_INF("USB CLI ready (open serial port to interact)");
	return 0;
}

int usb_cli_suspend(void)
{
	int err = 0;

	if (usb_suspended) {
		return 0;
	}

	irq_enabled = false;
	banner_shown = false;
	line_len = 0;
	ring_buf_reset(&rx_ring);

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
	if (udc_dev != NULL) {
		err = udc_disable(udc_dev);
		if (err && err != -EALREADY) {
			LOG_WRN("udc_disable: %d", err);
		}
	}
#else
	err = usb_disable();
	if (err && err != -EALREADY) {
		LOG_WRN("usb_disable: %d", err);
	}
#endif

	usb_suspended = true;
	LOG_INF("USB suspended");
	return 0;
}

int usb_cli_resume(void)
{
	int err = 0;

	if (!usb_suspended) {
		return 0;
	}

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
	if (udc_dev != NULL) {
		err = udc_enable(udc_dev);
		if (err && err != -EALREADY) {
			LOG_ERR("udc_enable: %d", err);
			return err;
		}
	}
#else
	err = usb_enable(NULL);
	if (err && err != -EALREADY) {
		LOG_ERR("usb_enable resume: %d", err);
		return err;
	}
#endif

	usb_suspended = false;
	banner_shown = false;
	irq_enabled = false;
	LOG_INF("USB resumed");
	return 0;
}

bool usb_cli_is_suspended(void)
{
	return usb_suspended;
}

void usb_cli_poll(struct app_ctx *ctx)
{
	uint8_t byte;

	if (usb_suspended) {
		return;
	}

	try_show_banner(ctx);

	if (!banner_shown) {
		return;
	}

	while (ring_buf_get(&rx_ring, &byte, 1) == 1) {
		if (byte == '\r') {
			continue;
		}

		if (byte == '\n') {
			line_buf[line_len] = '\0';
			handle_line(ctx, line_buf);
			line_len = 0;
			continue;
		}

		if (line_len + 1 < LINE_MAX) {
			line_buf[line_len++] = (char)byte;
		} else {
			line_len = 0;
			reply_err("line_too_long", UNIFYING_SUCCESS);
		}
	}
}
