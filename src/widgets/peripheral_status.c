/*
 * peripheral_status.c
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/split/transport/peripheral.h>      // for ZMK_SPLIT_TRANSPORT_PERIPHERAL_REGISTER :contentReference[oaicite:2]{index=2}
#include <zmk/split/transport/types.h>           // for zmk_split_transport_central_command :contentReference[oaicite:3]{index=3}
#include <raw_hid/events.h>                      // for raw_hid_received_event & raise_raw_hid_received_event()

#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
#include <nice_view_hid/hid.h>
#endif

#include "peripheral_status.h"  // must declare struct zmk_widget_status plus draw_top()

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/*--------------------------------------------------------
 * 1) RAW HID: override the split-transport command handler
 *--------------------------------------------------------*/

// Forward-declare the core handler (weak in ZMK split transport)
extern int __zmk_split_transport_peripheral_command_handler(
    const struct zmk_split_transport_peripheral *transport,
    struct zmk_split_transport_central_command cmd);

// Our interceptor: after core runs, catch RAW_HID and re-emit
static int raw_hid_cmd_interceptor(
    const struct zmk_split_transport_peripheral *transport,
    struct zmk_split_transport_central_command cmd) {

    // Let ZMK’s central-command plumbing run first
    int ret = __zmk_split_transport_peripheral_command_handler(transport, cmd);

    if (cmd.type == ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_RAW_HID) {
        struct raw_hid_received_event evt = {
            .data   = cmd.data.raw_hid.data,
            .length = sizeof(cmd.data.raw_hid.data),
        };
        raise_raw_hid_received_event(evt);
    }
    return ret;
}

// Register our interceptor in the split-transport registry
static const struct zmk_split_transport_peripheral_api periph_transport_api = {
    .report_event = NULL,  // not used for central commands
};
ZMK_SPLIT_TRANSPORT_PERIPHERAL_REGISTER(raw_hid_periph, &periph_transport_api);

/*--------------------------------------------------------
 * 2) BATTERY WIDGET
 *--------------------------------------------------------*/
static void set_battery_status(struct zmk_widget_status *w,
                               struct battery_status_state s) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    w->state.charging = s.usb_present;
#endif
    w->state.battery = s.level;
    draw_top(w->obj, w->cbuf, &w->state);
}

static void battery_update_cb(struct battery_status_state s) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        set_battery_status(w, s);
    }
}

static struct battery_status_state battery_get_state(const zmk_event_t *e) {
    ARG_UNUSED(e);
    return (struct battery_status_state){
        .level       = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status,
                            struct battery_status_state,
                            battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

/*--------------------------------------------------------
 * 3) CONNECTION ICON WIDGET
 *--------------------------------------------------------*/
struct peripheral_status_state {
    bool connected;
};

static struct peripheral_status_state conn_get_state(const zmk_event_t *e) {
    ARG_UNUSED(e);
    return (struct peripheral_status_state){
        .connected = zmk_split_bt_peripheral_is_connected()
    };
}

static void conn_update_cb(struct peripheral_status_state st) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.connected = st.connected;
        draw_top(w->obj, w->cbuf, &w->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status,
                            struct peripheral_status_state,
                            conn_update_cb,
                            conn_get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed)

/*--------------------------------------------------------
 * 4) NOW PLAYING MEDIA INFO
 *--------------------------------------------------------*/
#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)

static struct media_title_notification get_title_notif(const zmk_event_t *e) {
    struct media_title_notification *ev = as_media_title_notification(e);
    return ev ? *ev : (struct media_title_notification){ .title = "" };
}

static void title_update_cb(struct media_title_notification n) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        strncpy(w->state.track_title, n.title, sizeof(w->state.track_title)-1);
        w->state.track_title[sizeof(w->state.track_title)-1] = '\0';
        lv_label_set_text(w->label_track,
                         w->state.track_title[0] ? w->state.track_title : "No media");
    }
}

static struct media_artist_notification get_artist_notif(const zmk_event_t *e) {
    struct media_artist_notification *ev = as_media_artist_notification(e);
    return ev ? *ev : (struct media_artist_notification){ .artist = "" };
}

static void artist_update_cb(struct media_artist_notification n) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        if (w->state.track_title[0]) {
            strncpy(w->state.track_artist, n.artist, sizeof(w->state.track_artist)-1);
            w->state.track_artist[sizeof(w->state.track_artist)-1] = '\0';
            lv_label_set_text(w->label_artist, w->state.track_artist);
        }
    }
}

static struct is_connected_notification get_media_conn_notif(const zmk_event_t *e) {
    struct is_connected_notification *ev = as_is_connected_notification(e);
    return ev ? *ev : (struct is_connected_notification){ .value = false };
}

static void media_conn_update_cb(struct is_connected_notification n) {
    struct zmk_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        lv_label_set_text(w->label_now, n.value ? "Now Playing" : "");
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_title,
    struct media_title_notification,    title_update_cb,    get_title_notif)
ZMK_SUBSCRIPTION(widget_media_title, media_title_notification)

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist,
    struct media_artist_notification, artist_update_cb,    get_artist_notif)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification)

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_conn,
    struct is_connected_notification, media_conn_update_cb, get_media_conn_notif)
ZMK_SUBSCRIPTION(widget_media_conn, is_connected_notification)

#endif  // CONFIG_NICE_VIEW_HID_MEDIA_INFO

/*--------------------------------------------------------
 * 5) WIDGET INIT
 *--------------------------------------------------------*/
int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    // Top-right battery/connection canvas
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    // Register & init listeners
    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();      // from ZMK_DISPLAY_WIDGET_LISTENER :contentReference[oaicite:4]{index=4}
    widget_peripheral_status_init();   // for connection icon :contentReference[oaicite:5]{index=5}

#if IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_INFO)
    // Now Playing labels
    widget->label_now    = lv_label_create(widget->obj);
    widget->label_track  = lv_label_create(widget->obj);
    widget->label_artist = lv_label_create(widget->obj);

    // Position & style them as you did before…

    // Hook up their init routines
    widget_media_conn_init();
    widget_media_title_init();
    widget_media_artist_init();
#endif

    // Initial render
    draw_top(widget->obj, widget->cbuf, &widget->state);  // only draw_top remains :contentReference[oaicite:6]{index=6}

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) {
    return widget->obj;
}
