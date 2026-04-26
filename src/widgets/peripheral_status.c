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
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#include "peripheral_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static void draw_top(struct zmk_widget_status *widget) {
    lv_obj_t *canvas = widget->top_canvas;

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, &widget->state);

    // Draw output status
    canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                     widget->state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    // Rotate canvas
    rotate_canvas(canvas);
}

static bool media_scroll_allowed(struct zmk_widget_status *widget) {
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
    return widget->state.is_connected &&
           (widget->state.charging ||
            widget->state.battery >= CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_MIN_BATTERY);
#else
    ARG_UNUSED(widget);
    return false;
#endif
}

static void apply_media_label_mode(struct zmk_widget_status *widget) {
    lv_label_long_mode_t mode =
        media_scroll_allowed(widget) ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : LV_LABEL_LONG_MODE_CLIP;

    lv_label_set_long_mode(widget->title_label, mode);
    lv_label_set_long_mode(widget->artist_label, mode);
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL)
    lv_obj_set_style_anim_duration(widget->title_label,
                                   CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_INTERVAL_MS, 0);
    lv_obj_set_style_anim_duration(widget->artist_label,
                                   CONFIG_NICE_VIEW_HID_MEDIA_SCROLL_INTERVAL_MS, 0);
#endif
}

static void update_now_playing_text(struct zmk_widget_status *widget) {
#if IS_ENABLED(CONFIG_RAW_HID)
    if (widget->title_label == NULL || widget->artist_label == NULL) {
        return;
    }

    apply_media_label_mode(widget);

    if (!widget->state.connected) {
        lv_label_set_text(widget->title_label, "Waiting link");
        lv_label_set_text(widget->artist_label, "Split offline");
        return;
    }

    if (!widget->state.is_connected) {
        lv_label_set_text(widget->title_label, "Waiting host");
        lv_label_set_text(widget->artist_label, "");
        return;
    }

    const bool has_title = strlen(widget->state.media_title) > 0;
    const bool has_artist = strlen(widget->state.media_artist) > 0;

    lv_label_set_text(widget->title_label,
                      has_title ? widget->state.media_title : "Now playing");
    lv_label_set_text(widget->artist_label, has_artist ? widget->state.media_artist : " ");
#else
    ARG_UNUSED(widget);
#endif
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
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget);
    update_now_playing_text(widget);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget);
    update_now_playing_text(widget);
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

        update_now_playing_text(widget);
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
        update_now_playing_text(widget);
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
        update_now_playing_text(widget);
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

    widget->top_canvas = lv_canvas_create(widget->obj);
    lv_obj_align(widget->top_canvas, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(widget->top_canvas, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE,
                         CANVAS_COLOR_FORMAT);

    widget->title_label = lv_label_create(widget->obj);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_LEFT, 6, 4);
    lv_obj_set_width(widget->title_label, 140);
    lv_label_set_long_mode(widget->title_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(widget->title_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(widget->title_label, LVGL_FOREGROUND, 0);

    widget->artist_label = lv_label_create(widget->obj);
    lv_obj_align(widget->artist_label, LV_ALIGN_TOP_LEFT, 6, 36);
    lv_obj_set_width(widget->artist_label, 140);
    lv_label_set_long_mode(widget->artist_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(widget->artist_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(widget->artist_label, LVGL_FOREGROUND, 0);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
#ifdef CONFIG_RAW_HID
    widget_is_connected_init();
    widget_media_title_init();
    widget_media_artist_init();
    update_now_playing_text(widget);
#endif
    draw_top(widget);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
