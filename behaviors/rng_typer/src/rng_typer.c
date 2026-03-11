/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: MIT
 *
 * ZMK Behavior: RNG Typer
 * Uses nRF52 hardware RNG to type:
 *   - Dice rolls: dXX (e.g. d20, d6, d100)
 *   - Full 32-bit raw random integer
 *   - Random 16-character string
 *
 * Behavior parameters (passed via keymap binding):
 *   param1 encodes BOTH the mode and an optional "send Enter" flag:
 *
 *     Bits [3:0] = mode
 *       0 = DICE   -> types "dN: XX" where N = param2 (sides)
 *       1 = INT    -> types the full raw 32-bit RNG value (param2 ignored)
 *       2 = STRING -> types a 16-char random string (param2 = charset selector)
 *
 *     Bit 7 (0x80) = send Enter after output
 *       Add 0x80 to param1 to append an Enter keypress after typing.
 *       Examples:
 *         &rng_typer 0    20   -> d20: N        (no enter)
 *         &rng_typer 0x80 20   -> d20: N <RET>  (with enter)
 *         &rng_typer 1    0    -> 3849204817
 *         &rng_typer 0x81 0    -> 3849204817 <RET>
 *         &rng_typer 2    3    -> aK3!mZx9...
 *         &rng_typer 0x82 3    -> aK3!mZx9... <RET>
 *
 *   param2 (mode=DICE)   = number of sides (e.g. 4, 6, 8, 10, 12, 20, 100)
 *   param2 (mode=INT)    = ignored; always full uint32
 *   param2 (mode=STRING) = charset: 0=alphanum, 1=hex, 2=lowercase, 3=alphanum+symbols
 */

#define DT_DRV_COMPAT zmk_behavior_rng_typer

#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── character tables ─────────────────────────────────────────── */

static const char CHARSET_ALPHANUM[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789";                          /* 62 chars */

static const char CHARSET_HEX[]       = "0123456789abcdef";  /* 16 chars */
static const char CHARSET_LOWER[]     = "abcdefghijklmnopqrstuvwxyz0123456789"; /* 36 */
static const char CHARSET_SYMBOLS[]   =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789!@#$%^&*()-_=+[]{}";        /* 80 chars */

/* ── RNG helper ───────────────────────────────────────────────── */

static const struct device *entropy_dev;

static int rng_init(void) {
    if (entropy_dev) return 0;
    entropy_dev = DEVICE_DT_GET(DT_NODELABEL(rng));
    if (!device_is_ready(entropy_dev)) {
        LOG_ERR("RNG device not ready");
        entropy_dev = NULL;
        return -ENODEV;
    }
    return 0;
}

/* Returns a uniform random uint32 */
static uint32_t rng_u32(void) {
    uint32_t val = 0;
    if (!entropy_dev) return 0;
    entropy_get_entropy(entropy_dev, (uint8_t *)&val, sizeof(val));
    return val;
}

/* Returns value in [0, bound) without modulo bias */
static uint32_t rng_range(uint32_t bound) {
    if (bound <= 1) return 0;
    uint32_t threshold = (0xFFFFFFFFU - bound + 1) % bound;
    uint32_t r;
    do { r = rng_u32(); } while (r < threshold);
    return r % bound;
}

/* ── key typing helpers ───────────────────────────────────────── */

/*
 * Delay between key events, matching ZMK's macro default.
 * CONFIG_ZMK_MACRO_DEFAULT_WAIT_MS is 30 ms by default.
 * Override in your board's .conf if needed:
 *   CONFIG_ZMK_RNG_TYPER_WAIT_MS=15   # faster but riskier on BLE
 *   CONFIG_ZMK_RNG_TYPER_WAIT_MS=50   # safer for slow/lossy hosts
 */
#ifndef CONFIG_ZMK_RNG_TYPER_WAIT_MS
#define CONFIG_ZMK_RNG_TYPER_WAIT_MS CONFIG_ZMK_MACRO_DEFAULT_WAIT_MS
#endif

static void type_keycode(uint32_t usage_page, uint32_t keycode, bool shifted) {
    if (shifted) {
        raise_zmk_keycode_state_changed_from_encoded(
            HID_USAGE_KEY | HID_USAGE(HID_USAGE_KEY, LEFT_SHIFT), true, k_uptime_get());
        k_msleep(CONFIG_ZMK_RNG_TYPER_WAIT_MS);
    }
    raise_zmk_keycode_state_changed_from_encoded(
        HID_USAGE_KEY | HID_USAGE(usage_page, keycode), true,  k_uptime_get());
    k_msleep(CONFIG_ZMK_RNG_TYPER_WAIT_MS);
    raise_zmk_keycode_state_changed_from_encoded(
        HID_USAGE_KEY | HID_USAGE(usage_page, keycode), false, k_uptime_get());
    k_msleep(CONFIG_ZMK_RNG_TYPER_WAIT_MS);
    if (shifted) {
        raise_zmk_keycode_state_changed_from_encoded(
            HID_USAGE_KEY | HID_USAGE(HID_USAGE_KEY, LEFT_SHIFT), false, k_uptime_get());
        k_msleep(CONFIG_ZMK_RNG_TYPER_WAIT_MS);
    }
}

/* Map an ASCII character to a ZMK keycode + shift flag */
static void type_ascii(char c) {
    /* digits */
    if (c >= '0' && c <= '9') {
        /* HID keycodes for 0-9: 0x27=0, 0x1E-0x26 = 1-9 */
        uint8_t kc = (c == '0') ? 0x27 : (0x1E + (c - '1'));
        type_keycode(HID_USAGE_KEY_KEYBOARD, kc, false);
        return;
    }
    /* lowercase */
    if (c >= 'a' && c <= 'z') {
        type_keycode(HID_USAGE_KEY_KEYBOARD, 0x04 + (c - 'a'), false);
        return;
    }
    /* uppercase */
    if (c >= 'A' && c <= 'Z') {
        type_keycode(HID_USAGE_KEY_KEYBOARD, 0x04 + (c - 'A'), true);
        return;
    }
    /* common symbols */
    switch (c) {
        case ' ':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x2C, false); break;
        case ':':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x33, true);  break;
        case '-':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x2D, false); break;
        case '_':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x2D, true);  break;
        case '!':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x1E, true);  break;
        case '@':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x1F, true);  break;
        case '#':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x20, true);  break;
        case '$':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x21, true);  break;
        case '%':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x22, true);  break;
        case '^':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x23, true);  break;
        case '&':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x24, true);  break;
        case '*':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x25, true);  break;
        case '(':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x26, true);  break;
        case ')':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x27, true);  break;
        case '+':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x2E, true);  break;
        case '=':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x2E, false); break;
        case '[':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x2F, false); break;
        case ']':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x30, false); break;
        case '{':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x2F, true);  break;
        case '}':  type_keycode(HID_USAGE_KEY_KEYBOARD, 0x30, true);  break;
        default: break; /* skip unknown */
    }
}

static void type_string(const char *s) {
    while (*s) { type_ascii(*s++); }
}

/* Type a decimal integer */
static void type_uint32(uint32_t n) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", n);
    type_string(buf);
}

/* Send an Enter keypress (HID Usage 0x28 = Return) */
static void type_enter(void) {
    type_keycode(HID_USAGE_KEY_KEYBOARD, 0x28, false);
}

/* ── mode implementations ─────────────────────────────────────── */

static void do_dice(uint32_t sides) {
    if (sides < 2) sides = 6;
    uint32_t roll = rng_range(sides) + 1;   /* 1-indexed */
    /* type: "d20: 15" */
    type_ascii('d');
    type_uint32(sides);
    type_ascii(':');
    type_ascii(' ');
    type_uint32(roll);
    LOG_DBG("Dice d%u → %u", sides, roll);
}

static void do_int(uint32_t unused) {
    /* Output the full raw 32-bit TRNG value: 0 – 4294967295 */
    uint32_t n = rng_u32();
    type_uint32(n);
    LOG_DBG("RNG full uint32 → %u", n);
}

static void do_string(uint32_t charset_id) {
#define STRING_LEN 16
    const char *charset;
    size_t clen;
    switch (charset_id) {
        case 1:  charset = CHARSET_HEX;     clen = sizeof(CHARSET_HEX)     - 1; break;
        case 2:  charset = CHARSET_LOWER;   clen = sizeof(CHARSET_LOWER)   - 1; break;
        case 3:  charset = CHARSET_SYMBOLS; clen = sizeof(CHARSET_SYMBOLS) - 1; break;
        default: charset = CHARSET_ALPHANUM;clen = sizeof(CHARSET_ALPHANUM)- 1; break;
    }
    for (int i = 0; i < STRING_LEN; i++) {
        type_ascii(charset[rng_range(clen)]);
    }
    LOG_DBG("RNG string typed (charset %u)", charset_id);
#undef STRING_LEN
}

/* ── ZMK behavior glue ────────────────────────────────────────── */

struct behavior_rng_typer_config { /* nothing needed at device level */ };

static int behavior_rng_typer_init(const struct device *dev) {
    return rng_init();
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    uint32_t param1   = binding->param1;
    uint32_t param2   = binding->param2;

    /* Bit 7 of param1 = "send Enter after output" flag */
    bool send_enter = (param1 & 0x80) != 0;
    uint32_t mode   =  param1 & 0x0F;

    switch (mode) {
        case 0: do_dice(param2);   break;
        case 1: do_int(param2);    break;
        case 2: do_string(param2); break;
        default:
            LOG_WRN("rng_typer: unknown mode %u", mode);
            break;
    }

    if (send_enter) {
        type_enter();
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
    NULL,
    NULL,
    NULL,
    POST_KERNEL,
    CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
    &behavior_rng_typer_driver_api
);
