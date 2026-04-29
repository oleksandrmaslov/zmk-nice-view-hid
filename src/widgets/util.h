/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <zephyr/sys/util.h>
#include <zmk/endpoints.h>
#include <nice_view_hid/hid.h>

#define NICE_VIEW_HID_PROFILE_COUNT 5
#define NICE_VIEW_HID_SCREEN_WIDTH 160
#define NICE_VIEW_HID_SCREEN_HEIGHT 68
#define NICE_VIEW_HID_PORTRAIT_WIDTH 68
#define NICE_VIEW_HID_PORTRAIT_HEIGHT 160

#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8
#define CANVAS_BUF_SIZE                                                                            \
    LV_CANVAS_BUF_SIZE(NICE_VIEW_HID_SCREEN_WIDTH, NICE_VIEW_HID_SCREEN_HEIGHT,                   \
                       LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT), LV_DRAW_BUF_STRIDE_ALIGN)

#define LVGL_BACKGROUND                                                                            \
    (IS_ENABLED(CONFIG_NICE_VIEW_HID_INVERTED) ? lv_color_hex(0xA3A3A3) : lv_color_hex(0x0C0C0C))
#define LVGL_FOREGROUND                                                                            \
    (IS_ENABLED(CONFIG_NICE_VIEW_HID_INVERTED) ? lv_color_hex(0x0C0C0C) : lv_color_hex(0xA3A3A3))

struct status_state {
    uint8_t battery;
    bool charging;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    struct zmk_endpoint_instance selected_endpoint;
    uint8_t active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_bonded[NICE_VIEW_HID_PROFILE_COUNT];
    uint8_t layer_index;
    const char *layer_label;
#elif IS_ENABLED(CONFIG_ZMK_SPLIT)
    bool connected;
#endif
    bool is_connected;
    uint8_t hour;
    uint8_t minute;
    uint8_t volume;
    uint8_t layout;
    char media_artist[NICE_VIEW_HID_TEXT_MAX_LEN + 1];
    char media_title[NICE_VIEW_HID_TEXT_MAX_LEN + 1];
};

struct battery_status_state {
    uint8_t level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool usb_present;
#endif
};

void init_root_obj(lv_obj_t *obj);
lv_obj_t *create_portrait_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                                lv_coord_t h, const lv_font_t *font, lv_text_align_t align,
                                lv_label_long_mode_t long_mode);
void set_label_text_if_changed(lv_obj_t *label, const char *text);
void set_label_hidden(lv_obj_t *label, bool hidden);

void draw_status_background(lv_obj_t *canvas);
void draw_battery(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                  const struct status_state *state);
void draw_ble_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, bool bonded, bool connected);
void draw_usb_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y);
void draw_language_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y);
void draw_volume_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, uint8_t volume);
void draw_play_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y);
void draw_profile_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, bool selected, bool bonded);

void copy_text_field(char *dst, const char *src);
void format_layout_label(uint8_t layout_index, char *buf, size_t buf_size);
