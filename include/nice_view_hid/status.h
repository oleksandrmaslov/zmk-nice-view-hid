/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>
#include "util.h"

struct status_state {
    uint8_t battery;
    bool charging;
    struct zmk_endpoint_instance selected_endpoint;
    uint8_t active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    uint8_t layer_index;
    const char *layer_label;
#ifdef CONFIG_RAW_HID
    uint8_t hour;
    uint8_t minute;
    uint8_t volume;
    uint8_t layout;
#endif
};

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_color_t top_buf[CANVAS_SIZE * CANVAS_SIZE];
#ifdef CONFIG_RAW_HID
    lv_color_t hid_buf[CANVAS_SIZE * CANVAS_SIZE];
#endif
    lv_color_t output_buf[CANVAS_SIZE * CANVAS_SIZE];
    lv_color_t layer_buf[CANVAS_SIZE * CANVAS_SIZE];
    struct status_state state;
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
