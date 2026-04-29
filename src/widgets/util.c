/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <stdio.h>
#include <string.h>
#include "util.h"

static void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t color) {
    lv_draw_rect_dsc_init(rect_dsc);
    rect_dsc->bg_color = color;
    rect_dsc->bg_opa = LV_OPA_COVER;
    rect_dsc->border_width = 0;
    rect_dsc->radius = 0;
}

static void map_portrait_rect(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                              lv_area_t *coords) {
    coords->x1 = NICE_VIEW_HID_PORTRAIT_HEIGHT - y - h;
    coords->y1 = x;
    coords->x2 = coords->x1 + h - 1;
    coords->y2 = coords->y1 + w - 1;
}

static void canvas_draw_mapped_rect(lv_obj_t *canvas, const lv_area_t *coords,
                                    lv_draw_rect_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect(&layer, draw_dsc, coords);
    lv_canvas_finish_layer(canvas, &layer);
}

static void canvas_draw_portrait_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                                      lv_coord_t w, lv_coord_t h,
                                      lv_draw_rect_dsc_t *draw_dsc) {
    if (w <= 0 || h <= 0) {
        return;
    }

    lv_area_t coords;
    map_portrait_rect(x, y, w, h, &coords);
    canvas_draw_mapped_rect(canvas, &coords, draw_dsc);
}

static void set_portrait_px(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_color_t color) {
    if (x < 0 || y < 0 || x >= NICE_VIEW_HID_PORTRAIT_WIDTH ||
        y >= NICE_VIEW_HID_PORTRAIT_HEIGHT) {
        return;
    }

    lv_canvas_set_px(canvas, NICE_VIEW_HID_PORTRAIT_HEIGHT - y - 1, x, color, LV_OPA_COVER);
}

static void draw_px_pattern(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                            const int8_t points[][2], size_t point_count, lv_color_t color) {
    for (size_t i = 0; i < point_count; i++) {
        set_portrait_px(canvas, x + points[i][0], y + points[i][1], color);
    }
}

void init_root_obj(lv_obj_t *obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, NICE_VIEW_HID_SCREEN_WIDTH, NICE_VIEW_HID_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(obj, LVGL_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *create_portrait_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                                lv_coord_t h, const lv_font_t *font, lv_text_align_t align,
                                lv_label_long_mode_t long_mode) {
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_remove_style_all(label);
    lv_obj_set_size(label, w, h);
    lv_obj_set_pos(label, NICE_VIEW_HID_PORTRAIT_HEIGHT - y, x);
    lv_obj_set_style_text_color(label, LVGL_FOREGROUND, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_pivot_x(label, 0, 0);
    lv_obj_set_style_transform_pivot_y(label, 0, 0);
    lv_obj_set_style_transform_rotation(label, 900, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_long_mode(label, long_mode);

    return label;
}

void set_label_text_if_changed(lv_obj_t *label, const char *text) {
    if (label == NULL || text == NULL) {
        return;
    }

    const char *current = lv_label_get_text(label);
    if (current == NULL || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

void set_label_hidden(lv_obj_t *label, bool hidden) {
    if (label == NULL) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

void draw_status_background(lv_obj_t *canvas) {
    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);
}

void draw_battery(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                  const struct status_state *state) {
    lv_draw_rect_dsc_t fg;
    init_rect_dsc(&fg, LVGL_FOREGROUND);
    lv_draw_rect_dsc_t bg;
    init_rect_dsc(&bg, LVGL_BACKGROUND);

    /* Pixel recreation of references/Connected.svg group "Battery". */
    canvas_draw_portrait_rect(canvas, x + 2, y + 0, 19, 1, &fg);
    canvas_draw_portrait_rect(canvas, x + 2, y + 10, 19, 1, &fg);
    canvas_draw_portrait_rect(canvas, x + 1, y + 1, 1, 1, &fg);
    canvas_draw_portrait_rect(canvas, x + 1, y + 9, 1, 1, &fg);
    canvas_draw_portrait_rect(canvas, x + 21, y + 1, 1, 1, &fg);
    canvas_draw_portrait_rect(canvas, x + 21, y + 9, 1, 1, &fg);
    canvas_draw_portrait_rect(canvas, x + 0, y + 2, 1, 7, &fg);
    canvas_draw_portrait_rect(canvas, x + 22, y + 2, 1, 7, &fg);
    canvas_draw_portrait_rect(canvas, x + 23, y + 4, 1, 3, &fg);

    uint8_t level = MIN(state->battery, (uint8_t)100);
    lv_coord_t fill_w = (level * 18 + 99) / 100;
    if (fill_w > 0) {
        canvas_draw_portrait_rect(canvas, x + 3, y + 2, fill_w, 7, &fg);
    }

    if (state->charging) {
        canvas_draw_portrait_rect(canvas, x + 6, y + 2, 8, 7, &bg);
        const int8_t bolt[][2] = {{12, 2}, {11, 3}, {10, 4}, {10, 5}, {9, 5},
                                  {12, 5}, {11, 6}, {10, 7}, {9, 8}};
        draw_px_pattern(canvas, x, y, bolt, ARRAY_SIZE(bolt), LVGL_FOREGROUND);
    }
}

void draw_ble_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, bool bonded, bool connected) {
    lv_draw_rect_dsc_t bg;
    init_rect_dsc(&bg, LVGL_BACKGROUND);

    if (!bonded) {
        const int8_t search[][2] = {{5, 0},  {6, 1},  {7, 2},  {8, 3}, {9, 5},
                                    {9, 8},  {8, 10}, {7, 11}, {6, 12}, {5, 13},
                                    {3, 2},  {2, 4},  {2, 9},  {3, 11}};
        draw_px_pattern(canvas, x, y, search, ARRAY_SIZE(search), LVGL_FOREGROUND);
        return;
    }

    /* Compact Bluetooth mark derived from Connected.svg group "Bluetooth". */
    const int8_t logo[][2] = {{5, 0},  {5, 1},  {5, 2},  {5, 3},  {5, 4},  {5, 5},
                              {5, 6},  {5, 7},  {5, 8},  {5, 9},  {5, 10}, {5, 11},
                              {5, 12}, {6, 1},  {7, 2},  {8, 3},  {7, 4},  {6, 5},
                              {4, 6},  {3, 5},  {2, 4},  {6, 7},  {7, 8},  {8, 9},
                              {7, 10}, {6, 11}, {4, 6},  {3, 7},  {2, 8}};
    draw_px_pattern(canvas, x, y, logo, ARRAY_SIZE(logo), LVGL_FOREGROUND);

    if (!connected) {
        canvas_draw_portrait_rect(canvas, x + 1, y + 5, 9, 3, &bg);
        for (uint8_t i = 0; i < 5; i++) {
            set_portrait_px(canvas, x + 1 + i * 2, y + 6, LVGL_FOREGROUND);
        }
    }
}

void draw_usb_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    const int8_t usb[][2] = {{5, 0},  {4, 1},  {5, 1},  {6, 1},  {5, 2},  {5, 3},
                             {5, 4},  {2, 4},  {3, 4},  {4, 4},  {6, 4},  {7, 4},
                             {8, 4},  {2, 5},  {8, 5},  {2, 6},  {5, 5},  {5, 6},
                             {5, 7},  {5, 8},  {4, 9},  {5, 9},  {6, 9},  {5, 10}};
    draw_px_pattern(canvas, x, y, usb, ARRAY_SIZE(usb), LVGL_FOREGROUND);
}

void draw_language_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    /* Direct pixel recreation of references/Language_icon.svg. */
    const int8_t globe[][2] = {
        {3, 0}, {4, 0}, {5, 0}, {6, 0}, {1, 1}, {2, 1}, {3, 1}, {6, 1}, {7, 1},
        {8, 1}, {1, 2}, {3, 2}, {6, 2}, {8, 2}, {0, 3}, {1, 3}, {2, 3}, {3, 3},
        {4, 3}, {5, 3}, {6, 3}, {7, 3}, {8, 3}, {9, 3}, {0, 4}, {2, 4}, {7, 4},
        {9, 4}, {0, 5}, {2, 5}, {7, 5}, {9, 5}, {0, 6}, {1, 6}, {2, 6}, {3, 6},
        {4, 6}, {5, 6}, {6, 6}, {7, 6}, {8, 6}, {9, 6}, {1, 7}, {3, 7}, {6, 7},
        {8, 7}, {1, 8}, {2, 8}, {3, 8}, {6, 8}, {7, 8}, {8, 8}, {3, 9}, {4, 9},
        {5, 9}, {6, 9},
    };
    draw_px_pattern(canvas, x, y, globe, ARRAY_SIZE(globe), LVGL_FOREGROUND);
}

void draw_volume_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, uint8_t volume) {
    const int8_t speaker[][2] = {{5, 0}, {4, 1}, {5, 1}, {3, 2}, {4, 2}, {0, 3},
                                 {1, 3}, {2, 3}, {4, 3}, {5, 3}, {0, 4}, {1, 4},
                                 {2, 4}, {4, 4}, {5, 4}, {0, 5}, {1, 5}, {2, 5},
                                 {4, 5}, {5, 5}, {3, 6}, {4, 6}, {4, 7}, {5, 7}};
    draw_px_pattern(canvas, x, y + 1, speaker, ARRAY_SIZE(speaker), LVGL_FOREGROUND);

    if (volume > 0) {
        const int8_t mid[][2] = {{7, 3}, {7, 4}, {7, 5}, {7, 6}, {8, 4}, {8, 5}};
        draw_px_pattern(canvas, x, y, mid, ARRAY_SIZE(mid), LVGL_FOREGROUND);
    }
    if (volume >= 60) {
        const int8_t loud[][2] = {{10, 0}, {11, 1}, {12, 2}, {12, 3}, {13, 4},
                                  {13, 5}, {12, 6}, {12, 7}, {11, 8}, {10, 9}};
        draw_px_pattern(canvas, x, y, loud, ARRAY_SIZE(loud), LVGL_FOREGROUND);
    }
}

void draw_play_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    /* From Peripheral.svg group "mdi:play", reduced to a 7x6 pixel triangle. */
    const int8_t play[][2] = {{0, 0}, {0, 1}, {1, 1}, {0, 2}, {1, 2}, {2, 2},
                              {0, 3}, {1, 3}, {2, 3}, {3, 3}, {0, 4}, {1, 4},
                              {2, 4}, {0, 5}, {1, 5}, {0, 6}};
    draw_px_pattern(canvas, x, y, play, ARRAY_SIZE(play), LVGL_FOREGROUND);
}

void draw_profile_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, bool selected, bool bonded) {
    lv_draw_rect_dsc_t fg;
    init_rect_dsc(&fg, LVGL_FOREGROUND);

    if (selected) {
        canvas_draw_portrait_rect(canvas, x, y, 10, 10, &fg);
        set_portrait_px(canvas, x, y, LVGL_BACKGROUND);
        set_portrait_px(canvas, x + 9, y, LVGL_BACKGROUND);
        set_portrait_px(canvas, x, y + 9, LVGL_BACKGROUND);
        set_portrait_px(canvas, x + 9, y + 9, LVGL_BACKGROUND);
        return;
    }

    if (bonded) {
        canvas_draw_portrait_rect(canvas, x + 1, y, 8, 1, &fg);
        canvas_draw_portrait_rect(canvas, x + 1, y + 9, 8, 1, &fg);
        canvas_draw_portrait_rect(canvas, x, y + 1, 1, 8, &fg);
        canvas_draw_portrait_rect(canvas, x + 9, y + 1, 1, 8, &fg);
        set_portrait_px(canvas, x + 1, y + 1, LVGL_FOREGROUND);
        set_portrait_px(canvas, x + 8, y + 1, LVGL_FOREGROUND);
        set_portrait_px(canvas, x + 1, y + 8, LVGL_FOREGROUND);
        set_portrait_px(canvas, x + 8, y + 8, LVGL_FOREGROUND);
        set_portrait_px(canvas, x, y, LVGL_BACKGROUND);
        set_portrait_px(canvas, x + 9, y, LVGL_BACKGROUND);
        set_portrait_px(canvas, x, y + 9, LVGL_BACKGROUND);
        set_portrait_px(canvas, x + 9, y + 9, LVGL_BACKGROUND);
        return;
    }

    const int8_t dotted[][2] = {{1, 0}, {3, 0}, {5, 0}, {7, 0}, {9, 1}, {9, 3},
                                {9, 5}, {9, 7}, {8, 9}, {6, 9}, {4, 9}, {2, 9},
                                {0, 8}, {0, 6}, {0, 4}, {0, 2}};
    draw_px_pattern(canvas, x, y, dotted, ARRAY_SIZE(dotted), LVGL_FOREGROUND);
}

void copy_text_field(char *dst, const char *src) {
#if IS_ENABLED(CONFIG_RAW_HID)
    if (dst == NULL) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, NICE_VIEW_HID_TEXT_MAX_LEN);
    dst[NICE_VIEW_HID_TEXT_MAX_LEN] = '\0';
#else
    ARG_UNUSED(dst);
    ARG_UNUSED(src);
#endif
}

void format_layout_label(uint8_t layout_index, char *buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) {
        return;
    }

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_SHOW_LAYOUT)
    const char *layouts = CONFIG_NICE_VIEW_HID_LAYOUTS;
    const char *start = layouts;
    uint8_t current_index = 0;

    while (*start != '\0') {
        const char *end = strchr(start, ',');
        size_t len = end == NULL ? strlen(start) : (size_t)(end - start);

        if (current_index == layout_index) {
            len = MIN(len, buf_size - 1);
            memcpy(buf, start, len);
            buf[len] = '\0';
            return;
        }

        if (end == NULL) {
            break;
        }

        start = end + 1;
        current_index++;
    }
#endif

    snprintf(buf, buf_size, "%u", layout_index);
}
