/*!
 * \file keymap.c
 * \brief 常用 HID 键盘 scancode 映射。
 */

#include "keymap.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

struct named_key {
	const char *name;
	uint8_t scancode;
};

/* USB HID Usage Tables - Keyboard/Keypad */
static const struct named_key named_keys[] = {
	{"enter", 0x28},
	{"esc", 0x29},
	{"escape", 0x29},
	{"bspc", 0x2A},
	{"backspace", 0x2A},
	{"tab", 0x2B},
	{"space", 0x2C},
	{"minus", 0x2D},
	{"equal", 0x2E},
	{"lbrace", 0x2F},
	{"rbrace", 0x30},
	{"bslash", 0x31},
	{"semi", 0x33},
	{"quote", 0x34},
	{"grave", 0x35},
	{"comma", 0x36},
	{"dot", 0x37},
	{"slash", 0x38},
	{"caps", 0x39},
	{"f1", 0x3A},
	{"f2", 0x3B},
	{"f3", 0x3C},
	{"f4", 0x3D},
	{"f5", 0x3E},
	{"f6", 0x3F},
	{"f7", 0x40},
	{"f8", 0x41},
	{"f9", 0x42},
	{"f10", 0x43},
	{"f11", 0x44},
	{"f12", 0x45},
	{"right", 0x4F},
	{"left", 0x50},
	{"down", 0x51},
	{"up", 0x52},
};

static uint8_t modifier_from_name(const char *name)
{
	if (strcmp(name, "ctrl") == 0 || strcmp(name, "lctrl") == 0) {
		return 0x01;
	}
	if (strcmp(name, "rctrl") == 0) {
		return 0x10;
	}
	if (strcmp(name, "shift") == 0 || strcmp(name, "lshift") == 0) {
		return 0x02;
	}
	if (strcmp(name, "rshift") == 0) {
		return 0x20;
	}
	if (strcmp(name, "alt") == 0 || strcmp(name, "lalt") == 0) {
		return 0x04;
	}
	if (strcmp(name, "ralt") == 0 || strcmp(name, "altgr") == 0) {
		return 0x40;
	}
	if (strcmp(name, "gui") == 0 || strcmp(name, "win") == 0 || strcmp(name, "meta") == 0) {
		return 0x08;
	}
	return 0;
}

static bool scancode_from_token(const char *token, uint8_t *scancode)
{
	size_t i;

	if (token[0] != '\0' && token[1] == '\0') {
		char c = (char)tolower((unsigned char)token[0]);

		if (c >= 'a' && c <= 'z') {
			*scancode = (uint8_t)(0x04 + (c - 'a'));
			return true;
		}
		if (c >= '1' && c <= '9') {
			*scancode = (uint8_t)(0x1E + (c - '1'));
			return true;
		}
		if (c == '0') {
			*scancode = 0x27;
			return true;
		}
	}

	for (i = 0; i < ARRAY_SIZE(named_keys); i++) {
		if (strcmp(token, named_keys[i].name) == 0) {
			*scancode = named_keys[i].scancode;
			return true;
		}
	}

	return false;
}

bool keymap_parse(const char *name, struct keymap_stroke *out)
{
	char buf[48];
	char token[24];
	size_t i = 0;
	size_t t = 0;
	uint8_t key_index = 0;
	bool have_key = false;

	if (name == NULL || out == NULL || name[0] == '\0') {
		return false;
	}

	if (strlen(name) >= sizeof(buf)) {
		return false;
	}

	memset(out, 0, sizeof(*out));

	for (i = 0; name[i] != '\0'; i++) {
		buf[i] = (char)tolower((unsigned char)name[i]);
	}
	buf[i] = '\0';

	i = 0;
	while (true) {
		char c = buf[i];

		if (c != '+' && c != '\0') {
			if (t + 1 < sizeof(token)) {
				token[t++] = c;
			}
			i++;
			continue;
		}

		token[t] = '\0';
		if (t > 0) {
			uint8_t mod = modifier_from_name(token);
			uint8_t scancode = 0;

			if (mod != 0) {
				out->modifiers |= mod;
			} else if (scancode_from_token(token, &scancode)) {
				if (key_index >= UNIFYING_KEYS_LEN) {
					return false;
				}
				out->keys[UNIFYING_KEYS_LEN - 1 - key_index] = scancode;
				key_index++;
				have_key = true;
			} else {
				return false;
			}
		}

		if (c == '\0') {
			break;
		}

		t = 0;
		i++;
	}

	return have_key || out->modifiers != 0;
}

void keymap_print_help(void)
{
	printk("keys: a-z 0-9 enter esc space tab bspc\n");
	printk("      f1-f12 left right up down\n");
	printk("mods: shift+a  ctrl+c  alt+tab  gui+e\n");
}
