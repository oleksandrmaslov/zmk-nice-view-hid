/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

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

#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
#include <nice_view_hid/hid.h>
#endif

#include "status.h"  // brings in widget_media_title_init, widget_media_artist_init, etc.

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
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

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)

/* Pull title from incoming event */
static struct media_title_notification get_title_notif(const zmk_event_t *eh) {
    struct media_title_notification *ev = as_media_title_notification(eh);
    return ev ? *ev : (struct media_title_notification){.title = ""};
}

static void title_update_cb(struct media_title_notification notif) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->label_track, notif.title);
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title, struct media_title_notification, title_update_cb,
                            get_title_notif)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification);

/* Pull artist from incoming event */
static struct media_artist_notification get_artist_notif(const zmk_event_t *eh) {
    struct media_artist_notification *ev = as_media_artist_notification(eh);
    return ev ? *ev : (struct media_artist_notification){.artist = ""};
}

static void artist_update_cb(struct media_artist_notification notif) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->label_artist, notif.artist);
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification,
                            artist_update_cb, get_artist_notif)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

/* Connection state for media */
static struct is_connected_notification get_media_conn_notif(const zmk_event_t *eh) {
    struct is_connected_notification *ev = as_is_connected_notification(eh);
    return ev ? *ev : (struct is_connected_notification){.value = false};
}

static void media_conn_update_cb(struct is_connected_notification notif) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->label_now, notif.value ? "Now Playing" : "");
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_conn, struct is_connected_notification,
                            media_conn_update_cb, get_media_conn_notif)
ZMK_SUBSCRIPTION(widget_media_conn, is_connected_notification);

#endif

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    /* Initialize media widget listeners */
    widget_media_title_init();
    widget_media_artist_init();
    widget_media_conn_init();

    /* "Now Playing" header */
    widget->label_now = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label_now, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_label_set_text(widget->label_now, "Now Playing");
    lv_obj_set_pos(widget->label_now, 0, 32);

    /* Track title (scrolling) */
    widget->label_track = lv_label_create(widget->obj);
    lv_obj_set_width(widget->label_track, CANVAS_SIZE);
    lv_obj_set_style_text_font(widget->label_track, &lv_font_montserrat_18, LV_STATE_DEFAULT);
    lv_label_set_long_mode(widget->label_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(widget->label_track, "");
    lv_obj_set_pos(widget->label_track, 0, 44);

    /* Artist (truncated) */
    widget->label_artist = lv_label_create(widget->obj);
    lv_obj_set_width(widget->label_artist, CANVAS_SIZE);
    lv_obj_set_style_text_font(widget->label_artist, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_label_set_long_mode(widget->label_artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(widget->label_artist, "");
    lv_obj_set_pos(widget->label_artist, 0, 56);
#endif

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
