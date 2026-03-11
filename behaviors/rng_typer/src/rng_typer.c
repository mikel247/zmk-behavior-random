/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: MIT
 *
 * ZMK Behavior: RNG Typer
 * Uses nRF52 hardware RNG (TRNG) to type random output via the ZMK
 * behavior queue — identical to how macros work internally.
 *
 * param1 bits [3:0] = mode:
 *   0 = DICE   -> types "dN: XX" (param2 = sides)
 *   1 = INT    -> types full raw 32-bit RNG value
 *   2 = STRING -> types 16-char random string (param2 = charset)
 * param1 bit 7 (0x80) = send Enter after output
 *
 * param2 (DICE)   = sides e.g. 4/6/8/10/12/20/100
 * param2 (INT)    = ignored
 * param2 (STRING) = 0=alphanum  1=hex  2=lowercase  3=alphanum+symbols
 */

#define DT_DRV_COMPAT zmk_behavior_rng_typer

#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>
#include <zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── character tables ─────────────────────────────────────────── */

static const char CHARSET_ALPHANUM[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789";
static const char CHARSET_HEX[]     = "0123456789abcdef";
static const char CHARSET_LOWER[]   = "abcdefghijklmnopqrstuvwxyz0123456789";
static const char CHARSET_SYMBOLS[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789!@#$%^&*()-_=+[]{}";

/* ── RNG ──────────────────────────────────────────────────────── */

static const struct device *entropy_dev;

static int rng_init_dev(void) {
    if (entropy_dev) return 0;
    entropy_dev = DEVICE_DT_GET(DT_NODELABEL(rng));
    if (!device_is_ready(entropy_dev)) {
        LOG_ERR("RNG device not ready");
        entropy_dev = NULL;
        return -ENODEV;
    }
    return 0;
}

static uint32_t rng_u32(void) {
    uint32_t val = 0;
    if (!entropy_dev) return 0;
    entropy_get_entropy(entropy_dev, (uint8_t *)&val, sizeof(val));
    return val;
}

static uint32_t rng_range(uint32_t bound) {
    if (bound <= 1) return 0;
    uint32_t threshold = (0xFFFFFFFFU - bound + 1) % bound;
    uint32_t r;
    do { r = rng_u32(); } while (r < threshold);
    return r % bound;
}

/* ── ASCII -> ZMK HID usage ───────────────────────────────────── 
 * ZMK_HID_USAGE(page, id) from zmk/keys.h produces the encoded
 * value that &kp / key_press behavior expects as param1.
 */
static uint32_t ascii_to_hid(char c) {
    if (c >= 'a' && c <= 'z')
        return ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_A + (c - 'a'));
    if (c >= 'A' && c <= 'Z')
        return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_A + (c - 'A')));
    if (c == '0')
        return ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
    if (c >= '1' && c <= '9')
        return ZMK_HID_USAGE(HID_USAGE_KEY,
                             HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (c - '1'));
    switch (c) {
        case ' ':  return ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR);
        case ':':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON));
        case '-':  return ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE);
        case '_':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE));
        case '!':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION));
        case '@':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_2_AND_AT));
        case '#':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_3_AND_HASH));
        case '$':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR));
        case '%':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT));
        case '^':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_6_AND_CARET));
        case '&':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND));
        case '*':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK));
        case '(':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS));
        case ')':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS));
        case '+':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS));
        case '=':  return ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS);
        case '[':  return ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE);
        case ']':  return ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE);
        case '{':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE));
        case '}':  return LS(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE));
        default:   return 0;
    }
}

/* ── behavior queue helpers ───────────────────────────────────── */

/* Get the key_press behavior device — same node &kp references */
static const struct device *kp_dev(void) {
    return zmk_behavior_get_binding(DT_LABEL(DT_ALIAS(kp)));
}

static void queue_hid(uint32_t position, uint32_t hid_usage) {
    if (hid_usage == 0) return;

    const struct device *dev = zmk_behavior_get_binding("key_press");
    if (!dev) {
        LOG_ERR("key_press behavior not found");
        return;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = "key_press",
        .param1 = hid_usage,
        .param2 = 0,
    };

    zmk_behavior_queue_add(position, binding, true,
                           CONFIG_ZMK_MACRO_DEFAULT_TAP_MS);
    zmk_behavior_queue_add(position, binding, false,
                           CONFIG_ZMK_MACRO_DEFAULT_WAIT_MS);
}

static void queue_str(uint32_t position, const char *s) {
    while (*s) queue_hid(position, ascii_to_hid(*s++));
}

static void queue_u32(uint32_t position, uint32_t n) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", n);
    queue_str(position, buf);
}

/* ── modes ────────────────────────────────────────────────────── */

static void do_dice(uint32_t position, uint32_t sides) {
    if (sides < 2) sides = 6;
    uint32_t roll = rng_range(sides) + 1;
    char buf[16];
    snprintf(buf, sizeof(buf), "d%u: %u", sides, roll);
    queue_str(position, buf);
    LOG_DBG("Dice d%u -> %u", sides, roll);
}

static void do_int(uint32_t position) {
    uint32_t n = rng_u32();
    queue_u32(position, n);
    LOG_DBG("RNG uint32 -> %u", n);
}

static void do_string(uint32_t position, uint32_t charset_id) {
    const char *charset;
    size_t clen;
    switch (charset_id) {
        case 1:  charset = CHARSET_HEX;     clen = sizeof(CHARSET_HEX)     - 1; break;
        case 2:  charset = CHARSET_LOWER;   clen = sizeof(CHARSET_LOWER)   - 1; break;
        case 3:  charset = CHARSET_SYMBOLS; clen = sizeof(CHARSET_SYMBOLS) - 1; break;
        default: charset = CHARSET_ALPHANUM;clen = sizeof(CHARSET_ALPHANUM)- 1; break;
    }
    for (int i = 0; i < 16; i++) {
        queue_hid(position, ascii_to_hid(charset[rng_range(clen)]));
    }
    LOG_DBG("RNG string queued (charset %u)", charset_id);
}

/* ── behavior driver ──────────────────────────────────────────── */

static int behavior_rng_typer_init(const struct device *dev) {
    return rng_init_dev();
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    uint32_t param1     = binding->param1;
    uint32_t param2     = binding->param2;
    uint32_t position   = event.position;
    bool     send_enter = (param1 & 0x80) != 0;
    uint32_t mode       =  param1 & 0x0F;

    LOG_DBG("rng_typer pressed: mode=%u param2=%u enter=%d", mode, param2, send_enter);

    switch (mode) {
        case 0: do_dice(position, param2);   break;
        case 1: do_int(position);            break;
        case 2: do_string(position, param2); break;
        default: LOG_WRN("rng_typer: unknown mode %u", mode); break;
    }

    if (send_enter) {
        queue_hid(position,
            ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_RETURN_ENTER));
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_rng_typer_driver_api = {
    .binding_pressed  = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(
    0,
    behavior_rng_typer_init,
    NULL, NULL, NULL,
    POST_KERNEL,
    CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
    &behavior_rng_typer_driver_api
);
