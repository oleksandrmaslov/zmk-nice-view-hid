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
#define CANVAS_SIZE   68
#define WIDGET_TOP     0
#define WIDGET_HID     1
#define WIDGET_MIDDLE  2
#define WIDGET_BOTTOM  3


struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_color_t cbuf[CANVAS_SIZE * CANVAS_SIZE];
    struct status_state state;
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    lv_obj_t *label_now;
    lv_obj_t *label_track;
    lv_obj_t *label_artist;
#endif
};

void rotate_canvas(lv_obj_t *canvas, lv_color_t cbuf[]);
void init_label_dsc(lv_draw_label_dsc_t *d, lv_color_t color,
                    const lv_font_t *font, lv_text_align_t align);
void init_rect_dsc(lv_draw_rect_dsc_t *d, lv_color_t bg_color);
void init_line_dsc(lv_draw_line_dsc_t *d, lv_color_t color, uint8_t width);
void init_arc_dsc(lv_draw_arc_dsc_t *d, lv_color_t color, uint8_t width);
void draw_battery(lv_obj_t *canvas, const struct status_state *state);

/* Canvas‐drawing routines from status.c */
void draw_top   (lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state);
void draw_hid   (lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state);
void draw_middle(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state);
void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
