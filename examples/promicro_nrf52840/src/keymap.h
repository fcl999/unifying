/*!
 * \file keymap.h
 * \brief 按键名称到 HID scancode / modifier 的映射。
 */

#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "unifying_const.h"

#ifdef __cplusplus
extern "C" {
#endif

struct keymap_stroke {
	uint8_t modifiers;
	uint8_t keys[UNIFYING_KEYS_LEN];
};

/*!
 * 解析按键名（如 "a"、"enter"、"shift+a"、"ctrl+c"）。
 *
 * \return true 解析成功。
 */
bool keymap_parse(const char *name, struct keymap_stroke *out);

/*!
 * 打印支持的按键列表到控制台。
 */
void keymap_print_help(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYMAP_H */
