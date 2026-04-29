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
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "peripheral_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static bool raw_hid_ready(const struct status_state *state) {
#if IS_ENABLED(CONFIG_RAW_HID)
    return state->is_connected;
#else
    ARG_UNUSED(state);
    return false;
#endif
}

static void draw_peripheral_canvas(struct zmk_widget_status *widget) {
    draw_status_background(widget->canvas);
    draw_battery(widget->canvas, 4, 6, &widget->state);
    draw_ble_icon(widget->canvas, 52, 4, true, widget->state.connected);

    if (widget->state.connected && raw_hid_ready(&widget->state)) {
        draw_play_icon(widget->canvas, 56, 29);
    }
}

static void update_peripheral_labels(struct zmk_widget_status *widget) {
    const bool split_ready = widget->state.connected;
    const bool hid_ready = split_ready && raw_hid_ready(&widget->state);

    set_label_hidden(widget->fallback_connect_label, split_ready ? hid_ready : true);
    set_label_hidden(widget->fallback_raw_hid_label, split_ready ? hid_ready : true);
    set_label_hidden(widget->title_label, split_ready && !hid_ready);
    set_label_hidden(widget->artist_label, split_ready && !hid_ready);
    set_label_hidden(widget->state_label, split_ready && !hid_ready);

    if (!split_ready) {
        set_label_hidden(widget->title_label, false);
        set_label_hidden(widget->artist_label, false);
        set_label_hidden(widget->state_label, false);
        set_label_text_if_changed(widget->title_label, "Peripheral");
        set_label_text_if_changed(widget->artist_label, "Waiting link");
        set_label_text_if_changed(widget->state_label, "Split offline");
        return;
    }

    if (!hid_ready) {
        set_label_text_if_changed(widget->fallback_connect_label, "Connect");
        set_label_text_if_changed(widget->fallback_raw_hid_label, "RAW HID");
        return;
    }

    set_label_text_if_changed(widget->title_label,
                              widget->state.media_title[0] != '\0' ? widget->state.media_title
                                                                    : "No title");
    set_label_text_if_changed(widget->artist_label,
                              widget->state.media_artist[0] != '\0' ? widget->state.media_artist
                                                                     : "No artist");
    set_label_text_if_changed(widget->state_label, "Playing");
}

static void refresh_widget(struct zmk_widget_status *widget) {
    draw_peripheral_canvas(widget);
    update_peripheral_labels(widget);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;

    refresh_widget(widget);
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
    refresh_widget(widget);
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

        refresh_widget(widget);
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
        update_peripheral_labels(widget);
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
        update_peripheral_labels(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_media_artist, struct media_artist_notification,
                            media_artist_update_cb, get_media_artist)
ZMK_SUBSCRIPTION(widget_media_artist, media_artist_notification);

#endif

static void init_canvas(struct zmk_widget_status *widget) {
    widget->canvas = lv_canvas_create(widget->obj);
    lv_obj_remove_style_all(widget->canvas);
    lv_obj_set_size(widget->canvas, NICE_VIEW_HID_SCREEN_WIDTH, NICE_VIEW_HID_SCREEN_HEIGHT);
    lv_obj_set_pos(widget->canvas, 0, 0);
    lv_canvas_set_buffer(widget->canvas, widget->canvas_buf, NICE_VIEW_HID_SCREEN_WIDTH,
                         NICE_VIEW_HID_SCREEN_HEIGHT, CANVAS_COLOR_FORMAT);
    lv_obj_move_to_index(widget->canvas, 0);
}

static void init_labels(struct zmk_widget_status *widget) {
    lv_label_long_mode_t media_long_mode =
        IS_ENABLED(CONFIG_NICE_VIEW_HID_MEDIA_SCROLL) ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR
                                                      : LV_LABEL_LONG_MODE_CLIP;

    widget->title_label = create_portrait_label(widget->obj, 4, 28, 60, 18,
                                                &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER,
                                                media_long_mode);
    widget->artist_label = create_portrait_label(widget->obj, 4, 50, 60, 14,
                                                 &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
                                                 media_long_mode);
    widget->state_label = create_portrait_label(widget->obj, 4, 68, 60, 12,
                                                &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
                                                LV_LABEL_LONG_MODE_CLIP);

    widget->fallback_connect_label = create_portrait_label(
        widget->obj, 4, 34, 60, 14, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
        LV_LABEL_LONG_MODE_CLIP);
    widget->fallback_raw_hid_label = create_portrait_label(
        widget->obj, 4, 51, 60, 24, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER,
        LV_LABEL_LONG_MODE_CLIP);
}

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    memset(widget, 0, sizeof(*widget));
    widget->obj = lv_obj_create(parent);
    init_root_obj(widget->obj);

    init_canvas(widget);
    init_labels(widget);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
#ifdef CONFIG_RAW_HID
    widget_is_connected_init();
    widget_media_title_init();
    widget_media_artist_init();
#endif

    refresh_widget(widget);
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
