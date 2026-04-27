/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <string.h>
#include "util.h"

/*
 * Battery and connectivity glyphs are adapted from kevinpastor/nice-view-elemental,
 * MIT licensed.
 */

#if CONFIG_NICE_VIEW_HID_INVERTED
#define ELEMENTAL_BG_COLOR_BYTES 0x00, 0x00, 0x00, 0xff
#define ELEMENTAL_FG_COLOR_BYTES 0xff, 0xff, 0xff, 0xff
#else
#define ELEMENTAL_BG_COLOR_BYTES 0xff, 0xff, 0xff, 0xff
#define ELEMENTAL_FG_COLOR_BYTES 0x00, 0x00, 0x00, 0xff
#endif

static const uint8_t bluetooth_logo_map[] = {
    ELEMENTAL_BG_COLOR_BYTES,
    ELEMENTAL_FG_COLOR_BYTES,
    0x0f, 0x00, 0x3f, 0xc0, 0x7f, 0xe0, 0x7b, 0xe0, 0xf9, 0xf0, 0xfa, 0xf0,
    0xeb, 0x70, 0xf2, 0xf0, 0xf9, 0xf0, 0xf2, 0xf0, 0xeb, 0x70, 0xfa, 0xf0,
    0xf9, 0xf0, 0x7b, 0xe0, 0x7f, 0xe0, 0x3f, 0xc0, 0x0f, 0x00,
};

static const uint8_t bluetooth_logo_outlined_map[] = {
    ELEMENTAL_BG_COLOR_BYTES,
    ELEMENTAL_FG_COLOR_BYTES,
    0x0f, 0x00, 0x30, 0xc0, 0x40, 0x20, 0x44, 0x20, 0x86, 0x10, 0x85, 0x10,
    0x94, 0x90, 0x8d, 0x10, 0x86, 0x10, 0x8d, 0x10, 0x94, 0x90, 0x85, 0x10,
    0x86, 0x10, 0x44, 0x20, 0x40, 0x20, 0x30, 0xc0, 0x0f, 0x00,
};

static const uint8_t bluetooth_searching_map[] = {
    ELEMENTAL_BG_COLOR_BYTES,
    ELEMENTAL_FG_COLOR_BYTES,
    0x0f, 0x00, 0x30, 0xc0, 0x40, 0x20, 0x40, 0x20, 0x82, 0x10, 0x81, 0x10,
    0x89, 0x10, 0x84, 0x90, 0x94, 0x90, 0x84, 0x90, 0x89, 0x10, 0x81, 0x10,
    0x82, 0x10, 0x40, 0x20, 0x40, 0x20, 0x30, 0xc0, 0x0f, 0x00,
};

static const uint8_t usb_logo_map[] = {
    ELEMENTAL_BG_COLOR_BYTES,
    ELEMENTAL_FG_COLOR_BYTES,
    0x00, 0x10, 0x00, 0x00, 0xf8, 0x00, 0x01, 0x10, 0x00,
    0xe2, 0x00, 0x80, 0xff, 0xff, 0xc0, 0xe0, 0x80, 0x80,
    0x00, 0x40, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x0c, 0x00,
};

static const lv_image_dsc_t bluetooth_logo = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_I1,
    .header.w = 12,
    .header.h = 17,
    .header.stride = 2,
    .data_size = sizeof(bluetooth_logo_map),
    .data = bluetooth_logo_map,
};

static const lv_image_dsc_t bluetooth_logo_outlined = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_I1,
    .header.w = 12,
    .header.h = 17,
    .header.stride = 2,
    .data_size = sizeof(bluetooth_logo_outlined_map),
    .data = bluetooth_logo_outlined_map,
};

static const lv_image_dsc_t bluetooth_searching = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_I1,
    .header.w = 12,
    .header.h = 17,
    .header.stride = 2,
    .data_size = sizeof(bluetooth_searching_map),
    .data = bluetooth_searching_map,
};

static const lv_image_dsc_t usb_logo = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_I1,
    .header.w = 18,
    .header.h = 9,
    .header.stride = 3,
    .data_size = sizeof(usb_logo_map),
    .data = usb_logo_map,
};

void rotate_canvas(lv_obj_t *canvas) {
    uint8_t *buf = lv_canvas_get_draw_buf(canvas)->data;
    static uint8_t buf_copy[CANVAS_BUF_SIZE];
    memcpy(buf_copy, buf, sizeof(buf_copy));

    const uint32_t stride = lv_draw_buf_width_to_stride(CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_draw_sw_rotate(buf_copy, buf, CANVAS_SIZE, CANVAS_SIZE, stride, stride,
                      LV_DISPLAY_ROTATION_270, CANVAS_COLOR_FORMAT);
}

void rotate_portrait_canvas(uint8_t *source_buf, uint8_t *dest_buf) {
    const uint32_t source_stride =
        lv_draw_buf_width_to_stride(NICE_VIEW_HID_PORTRAIT_WIDTH, CANVAS_COLOR_FORMAT);
    const uint32_t dest_stride =
        lv_draw_buf_width_to_stride(NICE_VIEW_HID_SCREEN_WIDTH, CANVAS_COLOR_FORMAT);

    lv_draw_sw_rotate(source_buf, dest_buf, NICE_VIEW_HID_PORTRAIT_WIDTH,
                      NICE_VIEW_HID_PORTRAIT_HEIGHT, source_stride, dest_stride,
                      LV_DISPLAY_ROTATION_270, CANVAS_COLOR_FORMAT);
}

static void canvas_set_px(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_color_t color) {
    lv_canvas_set_px(canvas, x, y, color, LV_OPA_COVER);
}

/*
 * Horizontal battery glyph derived 1:1 from references/Connected.svg (#Battery group).
 * Origin (x, y) corresponds to SVG coordinate (4, 6); the icon spans (x..x+23, y..y+10).
 *
 * Layout (case is always drawn; fill scales with `level`):
 *   - left endcap: column at x+0, y+2..y+8
 *   - top/bottom edges: rows at y+0 and y+10, x+2..x+20
 *   - rounded outer corners: pixels at (x+1, y+1), (x+1, y+9), (x+21, y+1), (x+21, y+9)
 *   - right inner edge: column at x+22, y+2..y+8
 *   - terminal nub: column at x+23, y+4..y+6
 *   - level fill: 14 cols × 7 rows starting at (x+3, y+2), softened left col at x+2
 */
static void draw_battery_case(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_rect_dsc_t fg;
    init_rect_dsc(&fg, LVGL_FOREGROUND);

    canvas_draw_rect(canvas, x + 0, y + 2, 1, 7, &fg);
    canvas_draw_rect(canvas, x + 2, y + 0, 19, 1, &fg);
    canvas_draw_rect(canvas, x + 2, y + 10, 19, 1, &fg);
    canvas_set_px(canvas, x + 1, y + 1, LVGL_FOREGROUND);
    canvas_set_px(canvas, x + 1, y + 9, LVGL_FOREGROUND);
    canvas_set_px(canvas, x + 21, y + 1, LVGL_FOREGROUND);
    canvas_set_px(canvas, x + 21, y + 9, LVGL_FOREGROUND);
    canvas_draw_rect(canvas, x + 22, y + 2, 1, 7, &fg);
    canvas_draw_rect(canvas, x + 23, y + 4, 1, 3, &fg);
}

static void draw_battery_lightning_bolt(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    /* 7×7 bolt centered over the fill area (origin x=fill_left, y=case y+2) */
    static const uint8_t bolt[7] = {0x18, 0x30, 0x60, 0xfe, 0x0c, 0x18, 0x30};
    for (uint8_t row = 0; row < 7; row++) {
        for (uint8_t col = 0; col < 7; col++) {
            if (bolt[row] & (1 << (6 - col))) {
                /* invert: draw background pixel where the fill is, foreground over the gap */
                canvas_set_px(canvas, x + col, y + row, LVGL_BACKGROUND);
            }
        }
    }
}

static void draw_elemental_battery_at(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                                      uint8_t level, bool charging) {
    lv_draw_rect_dsc_t fg;
    init_rect_dsc(&fg, LVGL_FOREGROUND);

    draw_battery_case(canvas, x, y);

    uint8_t clamped = level > 100 ? 100 : level;
    /* main fill: up to 14 cols × 7 rows from (x+3, y+2) */
    uint8_t fill_w = ((uint16_t)14 * clamped + 50) / 100;
    if (fill_w > 14) fill_w = 14;

    if (fill_w > 0) {
        canvas_draw_rect(canvas, x + 3, y + 2, fill_w, 7, &fg);
        /* soft-rounded leftmost column when the fill reaches it */
        canvas_draw_rect(canvas, x + 2, y + 3, 1, 5, &fg);
    }

    if (charging && fill_w > 0) {
        /* draw bolt over the fill, centered on the visible body */
        draw_battery_lightning_bolt(canvas, x + 7, y + 2);
    }
}

void draw_battery(lv_obj_t *canvas, const struct status_state *state) {
    draw_elemental_battery_at(canvas, 4, 6, state->battery, state->charging);
}

void draw_elemental_bluetooth_logo(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    canvas_draw_img(canvas, x, y, &bluetooth_logo, &img_dsc);
}

void draw_elemental_bluetooth_logo_outlined(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    canvas_draw_img(canvas, x, y, &bluetooth_logo_outlined, &img_dsc);
}

void draw_elemental_bluetooth_searching(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    canvas_draw_img(canvas, x, y, &bluetooth_searching, &img_dsc);
}

void draw_elemental_usb_logo(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    canvas_draw_img(canvas, x, y, &usb_logo, &img_dsc);
}

void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align) {
    lv_draw_label_dsc_init(label_dsc);
    label_dsc->color = color;
    label_dsc->font = font;
    label_dsc->align = align;
}

void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color) {
    lv_draw_rect_dsc_init(rect_dsc);
    rect_dsc->bg_color = bg_color;
    rect_dsc->bg_opa = LV_OPA_COVER;
}

void init_line_dsc(lv_draw_line_dsc_t *line_dsc, lv_color_t color, uint8_t width) {
    lv_draw_line_dsc_init(line_dsc);
    line_dsc->color = color;
    line_dsc->width = width;
}

void init_arc_dsc(lv_draw_arc_dsc_t *arc_dsc, lv_color_t color, uint8_t width) {
    lv_draw_arc_dsc_init(arc_dsc);
    arc_dsc->color = color;
    arc_dsc->width = width;
}

void canvas_draw_line(lv_obj_t *canvas, const lv_point_t points[], uint32_t point_cnt,
                      lv_draw_line_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    for (uint32_t i = 1; i < point_cnt; ++i) {
        draw_dsc->p1.x = points[i - 1].x;
        draw_dsc->p1.y = points[i - 1].y;
        draw_dsc->p2.x = points[i].x;
        draw_dsc->p2.y = points[i].y;
        lv_draw_line(&layer, draw_dsc);
    }

    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      lv_draw_rect_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_area_t coords = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_arc(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t r,
                     int32_t start_angle, int32_t end_angle, lv_draw_arc_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->center.x = x;
    draw_dsc->center.y = y;
    draw_dsc->radius = r;
    draw_dsc->start_angle = start_angle;
    draw_dsc->end_angle = end_angle;
    lv_draw_arc(&layer, draw_dsc);

    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                      lv_draw_label_dsc_t *draw_dsc, const char *txt) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->text = txt;
    lv_area_t coords = {x, y, x + max_w - 1, y + lv_obj_get_height(canvas) - 1};
    lv_draw_label(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_rotated_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                              int32_t rotation, lv_draw_label_dsc_t *draw_dsc, const char *txt) {
    int32_t previous_rotation = draw_dsc->rotation;

    draw_dsc->rotation = rotation;
    canvas_draw_text(canvas, x, y, max_w, draw_dsc, txt);
    draw_dsc->rotation = previous_rotation;
}

void canvas_draw_img(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, const lv_image_dsc_t *src,
                     lv_draw_image_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->src = src;
    lv_area_t coords = {x, y, x + src->header.w - 1, y + src->header.h - 1};
    lv_draw_image(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}
