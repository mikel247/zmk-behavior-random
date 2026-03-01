/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#define DT_DRV_COMPAT zmk_behavior_magic_rng

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/keys.h>
#include <stdio.h>


static void tap_char(char c) {
    zmk_keycode_t key;
    if (c >= '0' && c <= '9') { key = (c == '0') ? N0 : (N1 + (c - '1')); }
    else { return; }
    zmk_endpoints_send_report(zmk_hid_keyboard_press(key));
    zmk_endpoints_send_report(zmk_hid_keyboard_release(key));
}

static int custom_random_handler(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    if (event.state != ZMK_BEHAVIOR_STATE_PRESSED) return 0;

    uint32_t rand_val;
    sys_csrand_get(&rand_val, sizeof(rand_val));

    // HARDCODED: 1-20 roll
    uint32_t result = (rand_val % 20) + 1;
    char buf[5];
    snprintf(buf, sizeof(buf), "%u", result);
    
    for (int i = 0; buf[i] != '\0'; i++) tap_char(buf[i]);

    // Hardcoded Enter Suffix
    zmk_endpoints_send_report(zmk_hid_keyboard_press(ENTER));
    zmk_endpoints_send_report(zmk_hid_keyboard_release(ENTER));

    return 0;
}

static const struct zmk_behavior_driver_api behavior_custom_random_driver_api = {
    .binding_handler = custom_random_handler
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_custom_random_driver_api);
