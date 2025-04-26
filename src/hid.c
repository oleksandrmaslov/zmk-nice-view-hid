#include <nice_view_hid/hid.h>
#include <raw_hid/events.h>

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(is_connected_notification);
ZMK_EVENT_IMPL(time_notification);
ZMK_EVENT_IMPL(volume_notification);

#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
ZMK_EVENT_IMPL(media_title_notification);
ZMK_EVENT_IMPL(media_artist_notification);
#endif

#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT
ZMK_EVENT_IMPL(layout_notification);
#endif

enum decode_id {
    _TIME          = 0xAA,
    _VOLUME        = 0xAB,
    _LAYOUT        = 0xAC,
#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
    _MEDIA_ARTIST  = 0xAD,
    _MEDIA_TITLE   = 0xAE,
#endif
};

static bool is_connected = false;

static void on_disconnect_timer(struct k_timer *dummy) {
    LOG_INF("raise_connection_notification: false");
    is_connected = false;
    raise_is_connected_notification((struct is_connected_notification){.value = false});
}

K_TIMER_DEFINE(disconnect_timer, on_disconnect_timer, NULL);

static uint8_t last_hid_volume = 0;
static uint8_t last_raised_volume = 0;

static void on_volume_timer(struct k_timer *dummy) {
    // prevent raising event with the same value multiple times
    if (last_raised_volume != last_hid_volume) {
        last_raised_volume = last_hid_volume;
        LOG_INF("raise_volume_notification %i", last_hid_volume);
        raise_volume_notification((struct volume_notification){.value = last_hid_volume});
    }
}

K_TIMER_DEFINE(volume_timer, on_volume_timer, NULL);

static int process_raw_hid_data(const uint8_t *data)
{
    switch (data[0]) {
    case _TIME:
        raise_time_notification((struct time_notification){.hour = data[1], .minute = data[2]});
        break;
    case _VOLUME:
        last_hid_volume = data[1];
        if (k_timer_status_get(&volume_timer) > 0 || k_timer_remaining_get(&volume_timer) == 0) {
            k_timer_start(&volume_timer, K_MSEC(150), K_NO_WAIT);
            on_volume_timer(&volume_timer);
        }
        break;
#ifdef CONFIG_NICE_VIEW_HID_MEDIA_INFO
    case _MEDIA_TITLE: {
        struct media_title_notification n = {0};
        strncpy(n.title, (char *)&data[2], sizeof(n.title)-1);
        raise_media_title_notification(n);
        break;
    }
    case _MEDIA_ARTIST: {
        struct media_artist_notification n = {0};
        strncpy(n.artist, (char *)&data[2], sizeof(n.artist)-1);
        raise_media_artist_notification(n);
        break;
    }
#endif
#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT
    case _LAYOUT:
        raise_layout_notification((struct layout_notification){.value = data[1]});
        break;
#endif
    }
    return 0;
}

static int raw_hid_received_event_listener(const zmk_event_t *eh) {
    struct raw_hid_received_event *event = as_raw_hid_received_event(eh);
    if (event) {
        process_raw_hid_data(event->data);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(process_raw_hid_event, raw_hid_received_event_listener);
ZMK_SUBSCRIPTION(process_raw_hid_event, raw_hid_received_event);
