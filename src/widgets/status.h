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

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *canvas;
    lv_obj_t *time_label;
    lv_obj_t *title_label;
    lv_obj_t *artist_label;
    lv_obj_t *layout_label;
    lv_obj_t *volume_label;
    lv_obj_t *profile_label;
    lv_obj_t *layer_heading_label;
    lv_obj_t *layer_label;
    lv_obj_t *fallback_connect_label;
    lv_obj_t *fallback_raw_hid_label;
    uint8_t canvas_buf[CANVAS_BUF_SIZE];
    struct status_state state;
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
