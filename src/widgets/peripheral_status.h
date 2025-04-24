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

struct peripheral_status_state {
    bool connected;
    char track_title[32];
    char track_artist[32];
    int battery;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool charging;
#endif
};

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_color_t cbuf[CANVAS_SIZE * CANVAS_SIZE];
    struct peripheral_status_state state;
#if defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    lv_obj_t *label_now;
    lv_obj_t *label_track;
    lv_obj_t *label_artist;
#endif
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
