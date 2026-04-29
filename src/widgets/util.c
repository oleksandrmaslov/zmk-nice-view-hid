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

/*
 * Custom UI bitmaps prepared by the user (references/*.c). The original
 * arrays were exported in LVGL 8 LV_IMG_CF_ALPHA_1BIT layout (no palette,
 * 1 bit/pixel, MSB first, padded to byte). We re-wrap that same pixel data
 * with the LVGL 9 LV_COLOR_FORMAT_I1 palette prefix used elsewhere in this
 * module so they integrate with the existing canvas draw helpers.
 */

/* Profile dots — 10×10, stride 2 */
static const uint8_t selected_profile_map[] = {
    ELEMENTAL_BG_COLOR_BYTES, ELEMENTAL_FG_COLOR_BYTES,
    0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0,
    0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0,
};
static const uint8_t bonded_profile_map[] = {
    ELEMENTAL_BG_COLOR_BYTES, ELEMENTAL_FG_COLOR_BYTES,
    0xff, 0xc0, 0xc0, 0xc0, 0x80, 0x40, 0x80, 0x40, 0x80, 0x40,
    0x80, 0x40, 0x80, 0x40, 0x80, 0x40, 0xc0, 0xc0, 0xff, 0xc0,
};
static const uint8_t free_profile_map[] = {
    ELEMENTAL_BG_COLOR_BYTES, ELEMENTAL_FG_COLOR_BYTES,
    0x55, 0x00, 0x00, 0x40, 0x80, 0x00, 0x00, 0x40, 0x80, 0x00,
    0x00, 0x40, 0x80, 0x00, 0x00, 0x40, 0x80, 0x00, 0x2a, 0x80,
};
static const uint8_t selected_free_profile_map[] = {
    ELEMENTAL_BG_COLOR_BYTES, ELEMENTAL_FG_COLOR_BYTES,
    0x55, 0x00, 0x7f, 0xc0, 0xff, 0x80, 0x7f, 0xc0, 0xff, 0x80,
    0x7f, 0xc0, 0xff, 0x80, 0x7f, 0xc0, 0xff, 0x80, 0x2a, 0x80,
};

/* 10×10 globe icon */
static const uint8_t language_icon_map[] = {
    ELEMENTAL_BG_COLOR_BYTES, ELEMENTAL_FG_COLOR_BYTES,
    0x1e, 0x00, 0x73, 0x80, 0x52, 0x80, 0xff, 0xc0, 0xa1, 0x40,
    0xa1, 0x40, 0xff, 0xc0, 0x52, 0x80, 0x73, 0x80, 0x1e, 0x00,
};

/* Volume / speaker glyphs */
static const uint8_t speaker_mute_map[] = {
    ELEMENTAL_BG_COLOR_BYTES, ELEMENTAL_FG_COLOR_BYTES,
    0x08, 0x00, 0x1a, 0x10, 0xf9, 0x20, 0xf8, 0xc0,
    0xf8, 0x40, 0xf9, 0x20, 0x1a, 0x10, 0x08, 0x00,
};
static const uint8_t speaker_middle_volume_map[] = {
    ELEMENTAL_BG_COLOR_BYTES, ELEMENTAL_FG_COLOR_BYTES,
    0x08, 0x18, 0xfa, 0xfb, 0xfb, 0xfa, 0x18, 0x08,
};
static const uint8_t volume_loud_wave_map[] = {
    ELEMENTAL_BG_COLOR_BYTES, ELEMENTAL_FG_COLOR_BYTES,
    0x80, 0x60, 0x20, 0x10, 0x10, 0x10, 0x10, 0x20, 0x60, 0x80,
};

#define DEFINE_I1_IMG(_name, _w, _h, _stride)                                                      \
    static const lv_image_dsc_t _name = {                                                          \
        .header.magic = LV_IMAGE_HEADER_MAGIC,                                                     \
        .header.cf = LV_COLOR_FORMAT_I1,                                                           \
        .header.w = (_w),                                                                          \
        .header.h = (_h),                                                                          \
        .header.stride = (_stride),                                                                \
        .data_size = sizeof(_name##_map),                                                          \
        .data = _name##_map,                                                                       \
    }

DEFINE_I1_IMG(selected_profile, 10, 10, 2);
DEFINE_I1_IMG(bonded_profile, 10, 10, 2);
DEFINE_I1_IMG(free_profile, 10, 10, 2);
DEFINE_I1_IMG(selected_free_profile, 10, 10, 2);
DEFINE_I1_IMG(language_icon, 10, 10, 2);
DEFINE_I1_IMG(speaker_mute, 12, 8, 2);
DEFINE_I1_IMG(speaker_middle_volume, 8, 8, 1);
DEFINE_I1_IMG(volume_loud_wave, 4, 10, 1);

static void draw_static_img(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                            const lv_image_dsc_t *src) {
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    canvas_draw_img(canvas, x, y, src, &dsc);
}

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
 * Horizontal battery — based on the upstream ZMK nice_view shape (which is
 * what the panel renders correctly without any per-canvas rotation). 33×12
 * total, terminal nub on the right.  Origin (x, y) is the top-left of the
 * case rectangle.  Charging bolt is the kevinpastor/nice-view-elemental
 * lightning Z, ported over and rotated 90° so it reads upright inside the
 * horizontal body.
 */
static void draw_battery_lightning_bolt(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    /*
     * 7×5 bolt; FG pixels are the bolt itself, BG pixels carve a 1-px halo
     * from the surrounding fill so the bolt stays visible at every level.
     * Pattern (relative to (x, y), col 0..6 / row 0..4):
     *
     *     col:  0  1  2  3  4  5  6
     *     row 0:  .  BG BG BG BG .  .
     *     row 1:  FG BG BG FG FG BG .
     *     row 2:  BG FG FG FG FG FG .
     *     row 3:  .  BG FG FG BG BG FG
     *     row 4:  .  .  BG BG BG .  BG
     */
    static const struct { int8_t dx, dy; bool fg; } pixels[] = {
        {0, 1, true},  {1, 2, true},  {2, 2, true},  {2, 3, true},
        {3, 1, true},  {3, 2, true},  {3, 3, true},  {4, 1, true},
        {4, 2, true},  {5, 2, true},  {6, 3, true},
        {1, 0, false}, {2, 0, false}, {3, 0, false}, {4, 0, false},
        {0, 2, false}, {1, 1, false}, {2, 1, false}, {5, 1, false},
        {1, 3, false}, {4, 3, false}, {5, 3, false},
        {2, 4, false}, {3, 4, false}, {4, 4, false}, {6, 4, false},
    };
    for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
        canvas_set_px(canvas, x + pixels[i].dx, y + pixels[i].dy,
                      pixels[i].fg ? LVGL_FOREGROUND : LVGL_BACKGROUND);
    }
}

static void draw_horizontal_battery_at(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                                       uint8_t level, bool charging) {
    lv_draw_rect_dsc_t fg, bg;
    init_rect_dsc(&fg, LVGL_FOREGROUND);
    init_rect_dsc(&bg, LVGL_BACKGROUND);

    /* outer case + inner cutout (matches upstream nice_view) */
    canvas_draw_rect(canvas, x, y, 29, 12, &fg);
    canvas_draw_rect(canvas, x + 1, y + 1, 27, 10, &bg);

    /* level fill: up to 25 cols × 8 rows from (x+2, y+2) */
    uint8_t clamped = level > 100 ? 100 : level;
    uint8_t fill_w = ((uint16_t)25 * clamped + 50) / 100;
    if (fill_w > 25) fill_w = 25;
    if (fill_w > 0) {
        canvas_draw_rect(canvas, x + 2, y + 2, fill_w, 8, &fg);
    }

    /* terminal nub (right side) */
    canvas_draw_rect(canvas, x + 29, y + 3, 3, 6, &fg);
    canvas_draw_rect(canvas, x + 30, y + 4, 1, 4, &bg);

    if (charging) {
        /* bolt 7w × 5h, centered in the 25×8 body interior — origin offset (9, 3) */
        draw_battery_lightning_bolt(canvas, x + 9, y + 3);
    }
}

void draw_battery(lv_obj_t *canvas, const struct status_state *state) {
    draw_horizontal_battery_at(canvas, 4, 4, state->battery, state->charging);
}

void draw_battery_at(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                     const struct status_state *state) {
    draw_horizontal_battery_at(canvas, x, y, state->battery, state->charging);
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

void draw_profile_selected(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    draw_static_img(canvas, x, y, &selected_profile);
}

void draw_profile_bonded(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    draw_static_img(canvas, x, y, &bonded_profile);
}

void draw_profile_free(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    draw_static_img(canvas, x, y, &free_profile);
}

void draw_profile_selected_free(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    draw_static_img(canvas, x, y, &selected_free_profile);
}

void draw_language_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    draw_static_img(canvas, x, y, &language_icon);
}

/*
 * Volume icon dispatcher.
 *   level == 0          → speaker_mute (12×8)
 *   level <= 50         → speaker_middle_volume (8×8) at the speaker position
 *   level > 50          → speaker_middle_volume + volume_loud_wave (4×10) appended
 *                          to the right of the speaker
 */
void draw_volume_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, uint8_t level) {
    if (level == 0) {
        /* mute: 12×8 — anchor by speaker body so visual center stays put */
        draw_static_img(canvas, x, y, &speaker_mute);
        return;
    }
    /* speaker + medium waves baked in (8×8) */
    draw_static_img(canvas, x, y, &speaker_middle_volume);
    if (level > 50) {
        /* extra outer wave (4×10) — sits flush to the right of the speaker block,
         * shifted up 1 px so its 10-tall extent is vertically centered on the 8-tall body. */
        draw_static_img(canvas, x + 8, y - 1, &volume_loud_wave);
    }
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
