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
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <raw_hid/events.h>

#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
#include <nice_view_hid/hid.h>
#endif

#include "peripheral_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* --------------------------------------------------------------------------
 * RAW_HID: forward split-peripheral RAW_HID packets into raw_hid_received_event
 * -------------------------------------------------------------------------- */
static int split_periph_to_rawhid(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *ev =
        as_zmk_split_peripheral_status_changed(eh);
    if (!ev) {
        return 0;
    }

    if (ev->type == ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_RAW_HID) {
        struct raw_hid_received_event evt = {
            .data   = (uint8_t *)ev->data,
            .length = ev->data_len,
        };
        raise_raw_hid_received_event(evt);
    }
    return 0;
}
ZMK_LISTENER(split_periph_to_rawhid, split_periph_to_rawhid);
ZMK_SUBSCRIPTION(split_periph_to_rawhid, zmk_split_peripheral_status_changed);

/* --------------------------------------------------------------------------
 * BATTERY WIDGET
 * -------------------------------------------------------------------------- */
static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;
    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        set_battery_status(w, state);
    }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level       = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status,
                            struct battery_status_state,
                            battery_status_update_cb,
                            battery_status_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

/* --------------------------------------------------------------------------
 * SPLIT PERIPHERAL CONNECTION ICON
 * -------------------------------------------------------------------------- */
struct peripheral_status_state {
    bool connected;
};

static struct peripheral_status_state get_state(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    return (struct peripheral_status_state){
        .connected = zmk_split_bt_peripheral_is_connected()
    };
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.connected = state.connected;
        draw_top(w->obj, w->cbuf, &w->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status,
                            struct peripheral_status_state,
                            output_status_update_cb,
                            get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

/* --------------------------------------------------------------------------
 * NOW PLAYING MEDIA INFO (Host → Peripheral)
 * -------------------------------------------------------------------------- */
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)

/* Title event → update widget->state.track_title + label */
static struct media_title_notification get_title_notif(const zmk_event_t *eh) {
    struct media_title_notification *ev = as_media_title_notification(eh);
    return ev ? *ev : (struct media_title_notification){ .title = "" };
}

static void title_update_cb(struct media_title_notification notif) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        strncpy(w->state.track_title, notif.title, sizeof(w->state.track_title) - 1);
        w->state.track_title[sizeof(w->state.track_title)-1] = '\0';
        lv_label_set_text(w->label_track,
            w->state.track_title[0] ? w->state.track_title : "No media");
    }
}

/* Artist event → update widget->state.track_artist + label */
static struct media_artist_notification get_artist_notif(const zmk_event_t *eh) {
    struct media_artist_notification *ev = as_media_artist_notification(eh);
    return ev ? *ev : (struct media_artist_notification){ .artist = "" };
}

static void artist_update_cb(struct media_artist_notification notif) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        if (w->state.track_title[0]) {
            strncpy(w->state.track_artist, notif.artist, sizeof(w->state.track_artist) - 1);
            w->state.track_artist[sizeof(w->state.track_artist)-1] = '\0';
            lv_label_set_text(w->label_artist, w->state.track_artist);
        }
    }
}

/* Connection event → show/hide the "Now Playing" header */
static struct is_connected_notification get_media_conn_notif(const zmk_event_t *eh) {
    struct is_connected_notification *ev = as_is_connected_notification(eh);
    return ev ? *ev : (struct is_connected_notification){ .value = false };
}

static void media_conn_update_cb(struct is_connected_notification notif) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        lv_label_set_text(w->label_now, notif.value ? "Now Playing" : "");
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title,
    struct media_title_notification, title_update_cb, get_title_notif)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification);

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist,
    struct media_artist_notification, artist_update_cb, get_artist_notif)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_conn,
    struct is_connected_notification, media_conn_update_cb, get_media_conn_notif)
ZMK_SUBSCRIPTION(widget_media_conn, is_connected_notification);

#endif  // CONFIG_NICE_VIEW_HID_MEDIA_INFO

/* --------------------------------------------------------------------------
 * WIDGET INIT
 * -------------------------------------------------------------------------- */
int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    // Main container
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    // Top-right canvas for battery & connection icon
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    // Register & init listeners
    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    /* "Now Playing" header */
    widget->label_now = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label_now, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_label_set_text_static(widget->label_now, "");
    lv_obj_set_pos(widget->label_now, 0, 32);

    /* Track title (scrolling) */
    widget->label_track = lv_label_create(widget->obj);
    lv_obj_set_width(widget->label_track, CANVAS_SIZE);
    lv_obj_set_style_text_font(widget->label_track, &lv_font_montserrat_18, LV_STATE_DEFAULT);
    lv_label_set_long_mode(widget->label_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(widget->label_track, "");
    lv_obj_set_pos(widget->label_track, 0, 44);

    /* Artist name (truncated) */
    widget->label_artist = lv_label_create(widget->obj);
    lv_obj_set_width(widget->label_artist, CANVAS_SIZE);
    lv_obj_set_style_text_font(widget->label_artist, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_label_set_long_mode(widget->label_artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(widget->label_artist, "");
    lv_obj_set_pos(widget->label_artist, 0, 56);

    /* Hook up media listeners */
    widget_media_conn_init();
    widget_media_title_init();
    widget_media_artist_init();
#endif

    // Initial draws
    draw_top   (widget->obj, widget->cbuf,     &widget->state);
    draw_middle(widget->obj, widget->cbuf2,    &widget->state);
    draw_bottom(widget->obj, widget->cbuf3,    &widget->state);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) {
    return widget->obj;
}
