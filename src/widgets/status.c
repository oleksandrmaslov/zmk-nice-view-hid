/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <stdio.h>

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

#ifdef CONFIG_RAW_HID
#include <nice_view_hid/hid.h>
#endif

#include "status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    uint8_t active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_bonded[NICE_VIEW_HID_PROFILE_COUNT];
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static bool raw_hid_ready(const struct status_state *state) {
#if IS_ENABLED(CONFIG_RAW_HID)
    return state->is_connected;
#else
    ARG_UNUSED(state);
    return false;
#endif
}

static void draw_connection_icon(lv_obj_t *canvas, const struct status_state *state) {
    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        draw_usb_icon(canvas, 53, 6);
        break;
    case ZMK_TRANSPORT_BLE:
        draw_ble_icon(canvas, 52, 4, state->active_profile_bonded,
                      state->active_profile_connected);
        break;
    default:
        draw_ble_icon(canvas, 52, 4, false, false);
        break;
    }
}

static void draw_profiles(lv_obj_t *canvas, const struct status_state *state) {
    for (uint8_t i = 0; i < NICE_VIEW_HID_PROFILE_COUNT; i++) {
        const bool selected = i == state->active_profile_index;
        const bool bonded = state->profiles_bonded[i];
        draw_profile_icon(canvas, 4 + i * 12, 113, selected, bonded);
    }
}

static void draw_status_canvas(struct zmk_widget_status *widget) {
    draw_status_background(widget->canvas);
    draw_battery(widget->canvas, 4, 6, &widget->state);
    draw_connection_icon(widget->canvas, &widget->state);

    if (raw_hid_ready(&widget->state)) {
        draw_play_icon(widget->canvas, 56, 47);
        draw_language_icon(widget->canvas, 5, 73);
        draw_volume_icon(widget->canvas, 5, 87, widget->state.volume);
    }

    draw_profiles(widget->canvas, &widget->state);
}

static void update_layer_labels(struct zmk_widget_status *widget) {
    char layer[16];

    if (widget->state.layer_label != NULL && widget->state.layer_label[0] != '\0') {
        snprintf(layer, sizeof(layer), "%s", widget->state.layer_label);
    } else if (widget->state.layer_index == 0) {
        snprintf(layer, sizeof(layer), "Base");
    } else {
        snprintf(layer, sizeof(layer), "Layer %u", widget->state.layer_index);
    }

    set_label_text_if_changed(widget->profile_label, "Profile");
    set_label_text_if_changed(widget->layer_heading_label, "Layer");
    set_label_text_if_changed(widget->layer_label, layer);
}

static void update_raw_hid_labels(struct zmk_widget_status *widget) {
    const bool ready = raw_hid_ready(&widget->state);

    set_label_hidden(widget->fallback_connect_label, ready);
    set_label_hidden(widget->fallback_raw_hid_label, ready);
    set_label_hidden(widget->time_label, !ready);
    set_label_hidden(widget->title_label, !ready);
    set_label_hidden(widget->artist_label, !ready);
    set_label_hidden(widget->layout_label, !ready);
    set_label_hidden(widget->volume_label, !ready);

    if (!ready) {
        set_label_text_if_changed(widget->fallback_connect_label, "Connect");
        set_label_text_if_changed(widget->fallback_raw_hid_label, "RAW HID");
        return;
    }

    char time[8];
    snprintf(time, sizeof(time), "%02u:%02u", widget->state.hour, widget->state.minute);
    set_label_text_if_changed(widget->time_label, time);

    set_label_text_if_changed(widget->title_label,
                              widget->state.media_title[0] != '\0' ? widget->state.media_title
                                                                    : "No title");
    set_label_text_if_changed(widget->artist_label,
                              widget->state.media_artist[0] != '\0' ? widget->state.media_artist
                                                                     : "No artist");

    char layout[12];
    format_layout_label(widget->state.layout, layout, sizeof(layout));
    set_label_text_if_changed(widget->layout_label, layout);

    char volume[8];
    snprintf(volume, sizeof(volume), "%u%%", widget->state.volume);
    set_label_text_if_changed(widget->volume_label, volume);
}

static void refresh_widget(struct zmk_widget_status *widget) {
    draw_status_canvas(widget);
    update_layer_labels(widget);
    update_raw_hid_labels(widget);
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

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;
    memcpy(widget->state.profiles_bonded, state->profiles_bonded,
           sizeof(widget->state.profiles_bonded));

    refresh_widget(widget);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    struct output_status_state state = {
        .selected_endpoint = zmk_endpoint_get_selected(),
#if IS_ENABLED(CONFIG_ZMK_BLE)
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
#endif
    };

#if IS_ENABLED(CONFIG_ZMK_BLE)
    for (uint8_t i = 0; i < MIN(NICE_VIEW_HID_PROFILE_COUNT, ZMK_BLE_PROFILE_COUNT); i++) {
        state.profiles_bonded[i] = !zmk_ble_profile_is_open(i);
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

    refresh_widget(widget);
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

        update_raw_hid_labels(widget);
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

        refresh_widget(widget);
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

        update_raw_hid_labels(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layout, struct layout_notification, layout_update_cb,
                            get_layout)
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
        update_raw_hid_labels(widget);
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
        update_raw_hid_labels(widget);
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

    widget->fallback_connect_label = create_portrait_label(
        widget->obj, 4, 34, 60, 14, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
        LV_LABEL_LONG_MODE_CLIP);
    widget->fallback_raw_hid_label = create_portrait_label(
        widget->obj, 4, 51, 60, 24, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER,
        LV_LABEL_LONG_MODE_CLIP);

    widget->time_label = create_portrait_label(widget->obj, 4, 25, 60, 18,
                                               &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER,
                                               LV_LABEL_LONG_MODE_CLIP);
    widget->title_label = create_portrait_label(widget->obj, 4, 45, 60, 13,
                                                &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
                                                media_long_mode);
    widget->artist_label = create_portrait_label(widget->obj, 4, 58, 60, 12,
                                                 &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
                                                 media_long_mode);
    widget->layout_label = create_portrait_label(widget->obj, 20, 73, 43, 11,
                                                 &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT,
                                                 LV_LABEL_LONG_MODE_CLIP);
    widget->volume_label = create_portrait_label(widget->obj, 20, 87, 43, 11,
                                                 &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT,
                                                 LV_LABEL_LONG_MODE_CLIP);
    widget->profile_label = create_portrait_label(widget->obj, 4, 99, 60, 11,
                                                  &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
                                                  LV_LABEL_LONG_MODE_CLIP);
    widget->layer_heading_label = create_portrait_label(
        widget->obj, 4, 130, 60, 11, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
        LV_LABEL_LONG_MODE_CLIP);
    widget->layer_label = create_portrait_label(widget->obj, 4, 143, 60, 14,
                                                &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER,
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

    refresh_widget(widget);
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
