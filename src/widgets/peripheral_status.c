/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "peripheral_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static void fill_canvas(lv_obj_t *canvas) {
    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);
}

static void draw_play_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    lv_draw_rect_dsc_t rect_dsc;
    init_rect_dsc(&rect_dsc, LVGL_FOREGROUND);

    static const uint8_t heights[] = {1, 3, 5, 7, 5, 3, 1};
    static const uint8_t offsets[] = {3, 2, 1, 0, 1, 2, 3};

    for (uint8_t i = 0; i < ARRAY_SIZE(heights); i++) {
        canvas_draw_rect(canvas, x + i, y + offsets[i], 1, heights[i], &rect_dsc);
    }
}

static void draw_header(lv_obj_t *canvas, const struct status_state *state) {
    draw_battery_right(canvas, state, 4, 6);

    if (state->connected) {
        draw_elemental_bluetooth_logo(canvas, 144, 3);
    } else {
        draw_elemental_bluetooth_logo_outlined(canvas, 144, 3);
    }
}

static const char *media_title_text(const struct status_state *state) {
#if IS_ENABLED(CONFIG_RAW_HID)
    if (!state->connected) {
        return "Waiting link";
    }

    if (!state->is_connected) {
        return "Connect RAW HID";
    }

    return strlen(state->media_title) > 0 ? state->media_title : "Now playing";
#else
    ARG_UNUSED(state);
    return "Peripheral";
#endif
}

static const char *media_artist_text(const struct status_state *state) {
#if IS_ENABLED(CONFIG_RAW_HID)
    if (!state->connected) {
        return "Split offline";
    }

    if (!state->is_connected) {
        return "Waiting host";
    }

    return strlen(state->media_artist) > 0 ? state->media_artist : " ";
#else
    return state->connected ? "Connected" : "Disconnected";
#endif
}

static void draw_media(lv_obj_t *canvas, const struct status_state *state) {
    lv_draw_label_dsc_t title_dsc;
    init_label_dsc(&title_dsc, LVGL_FOREGROUND, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
    lv_draw_label_dsc_t artist_dsc;
    init_label_dsc(&artist_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_draw_label_dsc_t status_dsc;
    init_label_dsc(&status_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);

    draw_play_icon(canvas, 136, 30);
    canvas_draw_text(canvas, 124, 42, 34, &status_dsc, state->connected ? "Playing" : "Offline");
    canvas_draw_text(canvas, 4, 45, 116, &artist_dsc, media_artist_text(state));
    canvas_draw_text(canvas, 20, 22, 112, &title_dsc, media_title_text(state));
}

static void redraw_widget(struct zmk_widget_status *widget) {
    fill_canvas(widget->screen_canvas);
    draw_header(widget->screen_canvas, &widget->state);
    draw_media(widget->screen_canvas, &widget->state);
    lv_obj_invalidate(widget->screen_canvas);
}

static void copy_text_field(char *dst, const char *src) {
#if IS_ENABLED(CONFIG_RAW_HID)
    strncpy(dst, src, NICE_VIEW_HID_TEXT_MAX_LEN);
    dst[NICE_VIEW_HID_TEXT_MAX_LEN] = '\0';
#else
    ARG_UNUSED(dst);
    ARG_UNUSED(src);
#endif
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif

    widget->state.battery = state.level;
    redraw_widget(widget);
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

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;
    redraw_widget(widget);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

#ifdef CONFIG_RAW_HID

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

        redraw_widget(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_is_connected, struct is_connected_notification,
                            is_hid_connected_update_cb, get_is_hid_connected)
ZMK_SUBSCRIPTION(widget_is_connected, is_connected_notification);

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
        redraw_widget(widget);
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
        redraw_widget(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification,
                            media_artist_update_cb, get_media_artist)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

#endif

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, NICE_VIEW_HID_SCREEN_WIDTH, NICE_VIEW_HID_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(widget->obj, LVGL_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(widget->obj, 0, 0);
    lv_obj_set_style_pad_all(widget->obj, 0, 0);
    memset(&widget->state, 0, sizeof(widget->state));

    widget->screen_canvas = lv_canvas_create(widget->obj);
    lv_obj_align(widget->screen_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_set_buffer(widget->screen_canvas, widget->screen_cbuf, NICE_VIEW_HID_SCREEN_WIDTH,
                         NICE_VIEW_HID_SCREEN_HEIGHT, CANVAS_COLOR_FORMAT);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
#ifdef CONFIG_RAW_HID
    widget_is_connected_init();
    widget_media_title_init();
    widget_media_artist_init();
#endif

    redraw_widget(widget);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
