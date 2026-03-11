/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: MIT
 *
 * ZMK Behavior: RNG Typer
 * Uses nRF52 hardware RNG (TRNG) to type random output via the ZMK
 * behavior queue — the same mechanism used by macros. This avoids
 * blocking the system workqueue with k_msleep().
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
 *       Examples:
 *         &rng_typer 0    20   -> d20: N
 *         &rng_typer 0x80 20   -> d20: N <RET>
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
#include <zmk/behavior_queue.h>
#include <zmk/hid.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>
#include <dt-bindings/zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── character tables ─────────────────────────────────────────── */

static const char CHARSET_ALPHANUM[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789";                          /* 62 chars */

static const char CHARSET_HEX[]     = "0123456789abcdef";            /* 16 chars */
static const char CHARSET_LOWER[]   = "abcdefghijklmnopqrstuvwxyz0123456789"; /* 36 */
static const char CHARSET_SYMBOLS[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789!@#$%^&*()-_=+[]{}";        /* 80 chars */

/* ── RNG helper ───────────────────────────────────────────────── */

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

/* ── keycode encoding ─────────────────────────────────────────── 
 * ZMK encodes keycodes as: (usage_page << 16) | hid_usage
 * For standard keyboard keys: page = 0x07, usage = HID keycode
 * Shifted keys use implicit_mods in the event, but the simplest
 * approach is to use ZMK's own LS() macro which encodes
 * LEFT_SHIFT as an implicit modifier into the keycode word.
 * Keys.h defines: LS(kc) = (MOD_LSFT << 16) | kc  ... actually
 * ZMK uses ZMK_HID_USAGE(page, id) encoding. We use the same
 * encoded values as &kp does, pulled from dt-bindings/zmk/keys.h
 * ──────────────────────────────────────────────────────────────*/

/* Map ASCII char to a ZMK encoded keycode (same as used by &kp) */
static uint32_t ascii_to_zmk_keycode(char c) {
    /* Lowercase letters */
    if (c >= 'a' && c <= 'z') return (c - 'a' + HID_USAGE_KEY_KEYBOARD_A);
    /* Uppercase — same keycode but with LSHFT modifier encoded */
    if (c >= 'A' && c <= 'Z') return LS(c - 'A' + HID_USAGE_KEY_KEYBOARD_A);
    /* Digits */
    if (c == '0') return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
    if (c >= '1' && c <= '9') return (c - '1' + HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
    /* Symbols */
    switch (c) {
        case ' ':  return HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        case ':':  return LS(HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON);
        case ';':  return HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON;
        case '-':  return HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE;
        case '_':  return LS(HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE);
        case '!':  return LS(HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
        case '@':  return LS(HID_USAGE_KEY_KEYBOARD_2_AND_AT);
        case '#':  return LS(HID_USAGE_KEY_KEYBOARD_3_AND_HASH);
        case '$':  return LS(HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR);
        case '%':  return LS(HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT);
        case '^':  return LS(HID_USAGE_KEY_KEYBOARD_6_AND_CARET);
        case '&':  return LS(HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND);
        case '*':  return LS(HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK);
        case '(':  return LS(HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS);
        case ')':  return LS(HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
        case '+':  return LS(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS);
        case '=':  return HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS;
        case '[':  return HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE;
        case ']':  return HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE;
        case '{':  return LS(HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE);
        case '}':  return LS(HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE);
        default:   return 0; /* skip unknown */
    }
}

/* ── behavior queue helpers ───────────────────────────────────── */

/*
 * Queue a single key tap (press + release) via zmk_behavior_queue_add.
 * This is non-blocking — identical to how ZMK macros work internally.
 * wait_ms is the delay *after* the release before the next event.
 */
static void queue_key(uint32_t position, uint32_t encoded_keycode, uint32_t tap_ms, uint32_t wait_ms) {
    if (encoded_keycode == 0) return;

    struct zmk_behavior_binding kp_binding = {
        .behavior_dev = "KEY_PRESS",
        .param1 = encoded_keycode,
        .param2 = 0,
    };

    zmk_behavior_queue_add(position, kp_binding, true,  tap_ms);
    zmk_behavior_queue_add(position, kp_binding, false, wait_ms);
}

static void queue_string(uint32_t position, const char *s) {
    while (*s) {
        uint32_t kc = ascii_to_zmk_keycode(*s++);
        queue_key(position, kc, CONFIG_ZMK_MACRO_DEFAULT_TAP_MS,
                                CONFIG_ZMK_MACRO_DEFAULT_WAIT_MS);
    }
}

static void queue_uint32(uint32_t position, uint32_t n) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", n);
    queue_string(position, buf);
}

/* ── mode implementations ─────────────────────────────────────── */

static void do_dice(uint32_t position, uint32_t sides) {
    if (sides < 2) sides = 6;
    uint32_t roll = rng_range(sides) + 1;
    char buf[16];
    snprintf(buf, sizeof(buf), "d%u: %u", sides, roll);
    queue_string(position, buf);
    LOG_DBG("Dice d%u -> %u", sides, roll);
}

static void do_int(uint32_t position) {
    uint32_t n = rng_u32();
    queue_uint32(position, n);
    LOG_DBG("RNG full uint32 -> %u", n);
}

static void do_string(uint32_t position, uint32_t charset_id) {
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
        uint32_t kc = ascii_to_zmk_keycode(charset[rng_range(clen)]);
        queue_key(position, kc, CONFIG_ZMK_MACRO_DEFAULT_TAP_MS,
                                CONFIG_ZMK_MACRO_DEFAULT_WAIT_MS);
    }
    LOG_DBG("RNG string queued (charset %u)", charset_id);
#undef STRING_LEN
}

/* ── ZMK behavior glue ────────────────────────────────────────── */

static int behavior_rng_typer_init(const struct device *dev) {
    return rng_init_dev();
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    uint32_t param1   = binding->param1;
    uint32_t param2   = binding->param2;
    uint32_t position = event.position;

    bool send_enter = (param1 & 0x80) != 0;
    uint32_t mode   =  param1 & 0x0F;

    switch (mode) {
        case 0: do_dice(position, param2);   break;
        case 1: do_int(position);            break;
        case 2: do_string(position, param2); break;
        default:
            LOG_WRN("rng_typer: unknown mode %u", mode);
            break;
    }

    if (send_enter) {
        queue_key(position, HID_USAGE_KEY_KEYBOARD_RETURN_ENTER,
                  CONFIG_ZMK_MACRO_DEFAULT_TAP_MS,
                  CONFIG_ZMK_MACRO_DEFAULT_WAIT_MS);
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
