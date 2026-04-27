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

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>

#include "status.h"

#ifdef CONFIG_RAW_HID
#include <nice_view_hid/hid.h>
#endif

enum widget_children {
    WIDGET_TOP = 0,
    WIDGET_HID,
    WIDGET_MIDDLE,
    WIDGET_BOTTOM,
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    uint8_t active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profile_connected[NICE_VIEW_HID_PROFILE_COUNT];
    bool profile_bonded[NICE_VIEW_HID_PROFILE_COUNT];
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void fill_canvas(lv_obj_t *canvas) {
    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);
}

static void draw_line(lv_obj_t *canvas, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
                      uint8_t width) {
    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, width);
    const lv_point_t points[] = {{x1, y1}, {x2, y2}};
    canvas_draw_line(canvas, points, ARRAY_SIZE(points), &line_dsc);
}

static void draw_background_pixel(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_rect_dsc_t rect_dsc;
    init_rect_dsc(&rect_dsc, LVGL_BACKGROUND);
    canvas_draw_rect(canvas, x, y, 1, 1, &rect_dsc);
}

static void draw_foreground_pixel(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_rect_dsc_t rect_dsc;
    init_rect_dsc(&rect_dsc, LVGL_FOREGROUND);
    canvas_draw_rect(canvas, x, y, 1, 1, &rect_dsc);
}

static void draw_open_profile_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    for (uint8_t i = 0; i < 10; i += 2) {
        draw_foreground_pixel(canvas, x + i, y);
        draw_foreground_pixel(canvas, x + i, y + 9);
        draw_foreground_pixel(canvas, x, y + i);
        draw_foreground_pixel(canvas, x + 9, y + i);
    }
}

static void draw_bonded_profile_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_rect_dsc_t fg_dsc;
    init_rect_dsc(&fg_dsc, LVGL_FOREGROUND);
    lv_draw_rect_dsc_t bg_dsc;
    init_rect_dsc(&bg_dsc, LVGL_BACKGROUND);

    canvas_draw_rect(canvas, x, y, 10, 10, &fg_dsc);
    canvas_draw_rect(canvas, x + 1, y + 1, 8, 8, &bg_dsc);
    draw_background_pixel(canvas, x, y);
    draw_background_pixel(canvas, x + 9, y);
    draw_background_pixel(canvas, x, y + 9);
    draw_background_pixel(canvas, x + 9, y + 9);
    draw_foreground_pixel(canvas, x + 1, y + 1);
    draw_foreground_pixel(canvas, x + 8, y + 1);
    draw_foreground_pixel(canvas, x + 1, y + 8);
    draw_foreground_pixel(canvas, x + 8, y + 8);
}

static void draw_connected_profile_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_rect_dsc_t fg_dsc;
    init_rect_dsc(&fg_dsc, LVGL_FOREGROUND);

    canvas_draw_rect(canvas, x, y, 10, 10, &fg_dsc);
    draw_background_pixel(canvas, x, y);
    draw_background_pixel(canvas, x + 9, y);
    draw_background_pixel(canvas, x, y + 9);
    draw_background_pixel(canvas, x + 9, y + 9);
}

static void draw_profile_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, bool connected,
                              bool bonded) {
    if (connected) {
        draw_connected_profile_icon(canvas, x, y);
    } else if (bonded) {
        draw_bonded_profile_icon(canvas, x, y);
    } else {
        draw_open_profile_icon(canvas, x, y);
    }
}

static void draw_profile_row(lv_obj_t *canvas, const struct status_state *state, lv_coord_t y) {
    static const lv_coord_t x_offsets[NICE_VIEW_HID_PROFILE_COUNT] = {4, 16, 28, 40, 52};

    for (uint8_t i = 0; i < NICE_VIEW_HID_PROFILE_COUNT; i++) {
        draw_profile_icon(canvas, x_offsets[i], y, i == state->active_profile_index,
                          state->profile_bonded[i]);
    }
}

static void draw_output_icon(lv_obj_t *canvas, const struct status_state *state) {
    if (state->selected_endpoint.transport == ZMK_TRANSPORT_USB) {
        draw_elemental_usb_logo(canvas, 45, 8);
        return;
    }

    if (state->active_profile_bonded) {
        if (state->active_profile_connected) {
            draw_elemental_bluetooth_logo(canvas, 52, 3);
        } else {
            draw_elemental_bluetooth_logo_outlined(canvas, 52, 3);
        }
    } else {
        draw_elemental_bluetooth_searching(canvas, 52, 3);
    }

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
    char profile[2] = {(char)('1' + MIN(state->active_profile_index,
                                         (uint8_t)(NICE_VIEW_HID_PROFILE_COUNT - 1))),
                       '\0'};
    canvas_draw_text(canvas, 36, 4, 14, &label_dsc, profile);
}

static void draw_language_icon(lv_obj_t *canvas, lv_coord_t cx, lv_coord_t cy) {
    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 1);
    canvas_draw_arc(canvas, cx, cy, 8, 0, 360, &arc_dsc);

    draw_line(canvas, cx - 8, cy, cx + 8, cy, 1);
    draw_line(canvas, cx, cy - 8, cx, cy + 8, 1);
    draw_line(canvas, cx - 6, cy - 4, cx + 6, cy - 4, 1);
    draw_line(canvas, cx - 6, cy + 4, cx + 6, cy + 4, 1);
}

static void draw_volume_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, uint8_t volume) {
    lv_draw_rect_dsc_t rect_dsc;
    init_rect_dsc(&rect_dsc, LVGL_FOREGROUND);

    canvas_draw_rect(canvas, x, y + 6, 4, 6, &rect_dsc);
    draw_line(canvas, x + 4, y + 6, x + 9, y + 2, 2);
    draw_line(canvas, x + 9, y + 2, x + 9, y + 16, 2);
    draw_line(canvas, x + 9, y + 16, x + 4, y + 12, 2);

    if (volume == 0) {
        draw_line(canvas, x + 13, y + 5, x + 19, y + 13, 1);
        draw_line(canvas, x + 19, y + 5, x + 13, y + 13, 1);
    } else {
        draw_line(canvas, x + 13, y + 7, x + 16, y + 10, 1);
        draw_line(canvas, x + 16, y + 10, x + 13, y + 13, 1);
        if (volume > 50) {
            draw_line(canvas, x + 16, y + 5, x + 20, y + 10, 1);
            draw_line(canvas, x + 20, y + 10, x + 16, y + 15, 1);
        }
    }
}

static void get_layout_text(uint8_t layout_index, char *layout, size_t layout_size) {
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_SHOW_LAYOUT)
    char layouts[sizeof(CONFIG_NICE_VIEW_HID_LAYOUTS)];
    strncpy(layouts, CONFIG_NICE_VIEW_HID_LAYOUTS, sizeof(layouts));
    layouts[sizeof(layouts) - 1] = '\0';

    char *current_layout = strtok(layouts, ",");
    size_t i = 0;
    while (current_layout != NULL && i < layout_index) {
        i++;
        current_layout = strtok(NULL, ",");
    }

    if (current_layout != NULL) {
        snprintf(layout, layout_size, "%s", current_layout);
    } else {
        snprintf(layout, layout_size, "%u", layout_index);
    }
#else
    ARG_UNUSED(layout_index);
    layout[0] = '\0';
#endif
}

static void draw_top(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_TOP);

    fill_canvas(canvas);
    draw_battery(canvas, state);
    draw_output_icon(canvas, state);
    rotate_canvas(canvas);
}

static void draw_hid(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_HID);

    lv_draw_label_dsc_t label_large;
    init_label_dsc(&label_large, LVGL_FOREGROUND, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_medium;
    init_label_dsc(&label_medium, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_small;
    init_label_dsc(&label_small, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    fill_canvas(canvas);

#if IS_ENABLED(CONFIG_RAW_HID)
    if (state->is_connected) {
        char time[8] = {};
        snprintf(time, sizeof(time), "%02u:%02u", state->hour, state->minute);
        canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_large, time);

        char layout[10] = {};
        get_layout_text(state->layout, layout, sizeof(layout));
        draw_language_icon(canvas, 13, 36);
        canvas_draw_text(canvas, 24, 28, 38, &label_medium, layout);

        char volume[5] = {};
        snprintf(volume, sizeof(volume), "%u", state->volume);
        draw_volume_icon(canvas, 6, 48, state->volume);
        canvas_draw_text(canvas, 31, 48, 30, &label_small, volume);
    } else
#endif
    {
        canvas_draw_text(canvas, 0, 12, CANVAS_SIZE, &label_medium, "Connect");
        canvas_draw_text(canvas, 0, 34, CANVAS_SIZE, &label_large, "RAW");
        canvas_draw_text(canvas, 0, 52, CANVAS_SIZE, &label_medium, "HID");
    }

    rotate_canvas(canvas);
}

static void draw_middle(lv_obj_t *widget, const struct status_state *state) {
    ARG_UNUSED(state);

    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_MIDDLE);

    fill_canvas(canvas);
    rotate_canvas(canvas);
}

static void draw_bottom(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, WIDGET_BOTTOM);

    lv_draw_label_dsc_t label_small;
    init_label_dsc(&label_small, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_draw_label_dsc_t label_center;
    init_label_dsc(&label_center, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    fill_canvas(canvas);

    canvas_draw_text(canvas, 4, 0, 60, &label_small, "Profile");
    draw_profile_row(canvas, state, 17);

    canvas_draw_text(canvas, 4, 37, 60, &label_small, "Layer");
    if (state->layer_label == NULL || strlen(state->layer_label) == 0) {
        char text[12] = {};
        snprintf(text, sizeof(text), "Base %u", state->layer_index);
        canvas_draw_text(canvas, 0, 50, CANVAS_SIZE, &label_center, text);
    } else {
        canvas_draw_text(canvas, 0, 50, CANVAS_SIZE, &label_center, state->layer_label);
    }

    rotate_canvas(canvas);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;

    draw_top(widget->obj, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;
    memcpy(widget->state.profile_connected, state->profile_connected,
           sizeof(widget->state.profile_connected));
    memcpy(widget->state.profile_bonded, state->profile_bonded,
           sizeof(widget->state.profile_bonded));

    draw_top(widget->obj, &widget->state);
    draw_bottom(widget->obj, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    struct output_status_state state = {
        .selected_endpoint = zmk_endpoint_get_selected(),
    };

#if defined(CONFIG_ZMK_BLE)
    state.active_profile_index = zmk_ble_active_profile_index();
    state.active_profile_connected = zmk_ble_active_profile_is_connected();
    state.active_profile_bonded = !zmk_ble_active_profile_is_open();

    for (uint8_t i = 0; i < NICE_VIEW_HID_PROFILE_COUNT; i++) {
        state.profile_connected[i] = zmk_ble_profile_is_connected(i);
        state.profile_bonded[i] = !zmk_ble_profile_is_open(i);
    }
#endif

    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;

    draw_bottom(widget->obj, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

#ifdef CONFIG_RAW_HID

static void copy_text_field(char *dst, const char *src) {
    strncpy(dst, src, NICE_VIEW_HID_TEXT_MAX_LEN);
    dst[NICE_VIEW_HID_TEXT_MAX_LEN] = '\0';
}

static struct is_connected_notification get_is_hid_connected(const zmk_event_t *eh) {
    struct is_connected_notification *notification = as_is_connected_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct is_connected_notification){.value = false};
}

static void is_hid_connected_update_cb(struct is_connected_notification is_connected) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.is_connected = is_connected.value;
        if (!is_connected.value) {
            widget->state.media_artist[0] = '\0';
            widget->state.media_title[0] = '\0';
        }

        draw_hid(widget->obj, &widget->state);
        draw_middle(widget->obj, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_is_connected, struct is_connected_notification,
                            is_hid_connected_update_cb, get_is_hid_connected)
ZMK_SUBSCRIPTION(widget_is_connected, is_connected_notification);

static struct time_notification get_time(const zmk_event_t *eh) {
    struct time_notification *notification = as_time_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct time_notification){.hour = 0, .minute = 0};
}

static void time_update_cb(struct time_notification time) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.hour = time.hour;
        widget->state.minute = time.minute;

        draw_hid(widget->obj, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_time, struct time_notification, time_update_cb, get_time)
ZMK_SUBSCRIPTION(widget_time, time_notification);

static struct volume_notification get_volume(const zmk_event_t *eh) {
    struct volume_notification *notification = as_volume_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct volume_notification){.value = 0};
}

static void volume_update_cb(struct volume_notification volume) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.volume = volume.value;

        draw_hid(widget->obj, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_volume, struct volume_notification, volume_update_cb, get_volume)
ZMK_SUBSCRIPTION(widget_volume, volume_notification);

#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT

static struct layout_notification get_layout(const zmk_event_t *eh) {
    struct layout_notification *notification = as_layout_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct layout_notification){.value = 0};
}

static void layout_update_cb(struct layout_notification layout) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.layout = layout.value;

        draw_hid(widget->obj, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layout, struct layout_notification, layout_update_cb, get_layout)
ZMK_SUBSCRIPTION(widget_layout, layout_notification);
#endif

static struct media_title_notification get_media_title(const zmk_event_t *eh) {
    struct media_title_notification *notification = as_media_title_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct media_title_notification){0};
}

static void media_title_update_cb(struct media_title_notification title) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        copy_text_field(widget->state.media_title, title.value);
        draw_middle(widget->obj, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title, struct media_title_notification,
                            media_title_update_cb, get_media_title)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification);

static struct media_artist_notification get_media_artist(const zmk_event_t *eh) {
    struct media_artist_notification *notification = as_media_artist_notification(eh);
    if (notification) {
        return *notification;
    }
    return (struct media_artist_notification){0};
}

static void media_artist_update_cb(struct media_artist_notification artist) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        copy_text_field(widget->state.media_artist, artist.value);
        draw_middle(widget->obj, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification,
                            media_artist_update_cb, get_media_artist)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

#endif

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_set_style_bg_color(widget->obj, LVGL_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    memset(&widget->state, 0, sizeof(widget->state));

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    lv_obj_t *hid = lv_canvas_create(widget->obj);
    lv_obj_align(hid, LV_ALIGN_TOP_LEFT, 64, 0);
    lv_canvas_set_buffer(hid, widget->cbuf_hid, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    lv_obj_t *middle = lv_canvas_create(widget->obj);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, -4, 0);
    lv_canvas_set_buffer(middle, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, -44, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_output_status_init();
    widget_layer_status_init();
#ifdef CONFIG_RAW_HID
    widget_is_connected_init();
    widget_time_init();
    widget_volume_init();
#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT
    widget_layout_init();
#endif
    widget_media_title_init();
    widget_media_artist_init();
#endif

    draw_top(widget->obj, &widget->state);
    draw_hid(widget->obj, &widget->state);
    draw_middle(widget->obj, &widget->state);
    draw_bottom(widget->obj, &widget->state);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
