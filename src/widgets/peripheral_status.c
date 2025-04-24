/*
 * Peripheral Status Widget Implementation
 *
 * Shows battery level and BLE peripheral connection status,
 * and "Now Playing" media info forwarded over Raw HID from the central.
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#ifdef CONFIG_RAW_HID
#include <nice_view_hid/hid.h>
#include <raw_hid/events.h>
#include <nice_view_hid/media_events.h>
#endif

#include "peripheral_status.h"

// Active widget instances
static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

// --- Internal Drawing ---
static void draw_status(lv_obj_t *parent, lv_color_t cbuf[], const struct status_state *st) {
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_draw_rect_dsc_t bg; init_rect_dsc(&bg, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &bg);

    // Battery icon/level
    draw_battery(canvas, st);

    // BLE connection icon
    lv_draw_label_dsc_t lbl; init_label_dsc(&lbl, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_RIGHT);
    const char *sym = zmk_split_bt_peripheral_is_connected() ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &lbl, sym);

    rotate_canvas(canvas, cbuf);
}

// --- Battery Listener ---
static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_status_state){
        .state_of_charge = ev ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}
static void battery_status_update_cb(struct battery_status_state bs) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.battery = bs.state_of_charge;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        w->state.charging = bs.usb_present;
#endif
        draw_status(w->obj, w->cbuf, &w->state);
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed)
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed)
#endif

// --- Peripheral Connection Listener ---
static struct peripheral_status_state get_conn(const zmk_event_t *evt) {
    ARG_UNUSED(evt);
    return (struct peripheral_status_state){ .connected = zmk_split_bt_peripheral_is_connected() };
}
static void connection_status_cb(struct peripheral_status_state ps) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.connected = ps.connected;
        draw_status(w->obj, w->cbuf, &w->state);
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            connection_status_cb, get_conn)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed)

#ifdef CONFIG_RAW_HID
// --- Media Title Listener ---
static struct media_title_notification get_media_title(const zmk_event_t *eh) {
    const struct media_title_notification *evt = as_media_title_notification(eh);
    return evt ? *evt : (struct media_title_notification){ .title = "" };
}
static void media_title_cb(struct media_title_notification mt) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        lv_label_set_text(w->label_track, mt.title[0] ? mt.title : "No media");
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title, struct media_title_notification,
                            media_title_cb, get_media_title)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification)

// --- Media Artist Listener ---
static struct media_artist_notification get_media_artist(const zmk_event_t *eh) {
    const struct media_artist_notification *evt = as_media_artist_notification(eh);
    return evt ? *evt : (struct media_artist_notification){ .artist = "" };
}
static void media_artist_cb(struct media_artist_notification ma) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        lv_label_set_text(w->label_artist, ma.artist);
    }
}
ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification,
                            media_artist_cb, get_media_artist)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification)
#endif

// --- Initialization ---
int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = parent;

    // Initial draw
    draw_status(widget->obj, widget->cbuf, &widget->state);

    // Register for updates
    sys_slist_append(&widgets, &widget->node);

#ifdef CONFIG_RAW_HID
#if defined(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    // Create "Now Playing" UI
    widget->label_now = lv_label_create(widget->obj);
    lv_label_set_text_static(widget->label_now, "Now Playing");
    lv_obj_set_pos(widget->label_now, 0, CANVAS_SIZE + 4);

    widget->label_track = lv_label_create(widget->obj);
    lv_label_set_long_mode(widget->label_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(widget->label_track, 0, CANVAS_SIZE + 24);

    widget->label_artist = lv_label_create(widget->obj);
    lv_label_set_long_mode(widget->label_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(widget->label_artist, 0, CANVAS_SIZE + 44);

    // Trigger initial media updates
    widget_media_title_init();
    widget_media_artist_init();
#endif
#endif

    // Trigger initial system updates
    widget_battery_status_init();
    widget_peripheral_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) {
    return widget->obj;
}
