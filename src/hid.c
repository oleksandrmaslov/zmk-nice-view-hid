#include <nice_view_hid/hid.h>
#include <raw_hid/events.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(is_connected_notification);
ZMK_EVENT_IMPL(time_notification);
ZMK_EVENT_IMPL(volume_notification);
ZMK_EVENT_IMPL(media_artist_notification);
ZMK_EVENT_IMPL(media_title_notification);
#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT
ZMK_EVENT_IMPL(layout_notification);
#endif

typedef enum {
    _TIME = 0xAA, // random value, must match companion app
    _VOLUME = 0xAB,
    _LAYOUT = 0xAC,
    _MEDIA_ARTIST = 0xAD,
    _MEDIA_TITLE = 0xAE,
    /*
     * Continuation packets append to the previously started media text. The
     * firmware accepts as many continuations as fit into
     * NICE_VIEW_HID_TEXT_MAX_LEN; remaining bytes are dropped with a log
     * warning. Companion apps that don't know about continuations just send
     * a single _MEDIA_TITLE / _MEDIA_ARTIST packet capped at the per-packet
     * limit (see hid.h for the full packet size discussion).
     */
    _MEDIA_ARTIST_CONT = 0xAF,
    _MEDIA_TITLE_CONT = 0xB0,
} hid_data_type;

#define HID_MAX_TEXT_LEN NICE_VIEW_HID_TEXT_MAX_LEN

static bool is_connected = false;

/*
 * Reassembly buffers for the chunked media text protocol. A new
 * _MEDIA_TITLE / _MEDIA_ARTIST packet always resets the corresponding
 * buffer; _MEDIA_*_CONT packets append.
 */
static char artist_buffer[NICE_VIEW_HID_TEXT_MAX_LEN + 1];
static char title_buffer[NICE_VIEW_HID_TEXT_MAX_LEN + 1];

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

static void emit_artist(void) {
    struct media_artist_notification notification = {0};
    strncpy(notification.value, artist_buffer, NICE_VIEW_HID_TEXT_MAX_LEN);
    notification.value[NICE_VIEW_HID_TEXT_MAX_LEN] = '\0';
    raise_media_artist_notification(notification);
}

static void emit_title(void) {
    struct media_title_notification notification = {0};
    strncpy(notification.value, title_buffer, NICE_VIEW_HID_TEXT_MAX_LEN);
    notification.value[NICE_VIEW_HID_TEXT_MAX_LEN] = '\0';
    raise_media_title_notification(notification);
}

static void handle_media_field(bool is_artist, bool is_continuation, uint8_t *data,
                               uint8_t length) {
    if (length < 2) {
        LOG_WRN("Media packet too short: %u", length);
        return;
    }

    uint8_t declared_len = data[1];
    uint8_t available_len = length > 2 ? length - 2 : 0;
    uint8_t copy_len = MIN(declared_len, available_len);

    char *buf = is_artist ? artist_buffer : title_buffer;

    if (!is_continuation) {
        memset(buf, 0, NICE_VIEW_HID_TEXT_MAX_LEN + 1);
    }

    size_t current = strlen(buf);
    if (current >= NICE_VIEW_HID_TEXT_MAX_LEN) {
        LOG_WRN("Media buffer full, dropping continuation chunk");
        return;
    }

    size_t remaining = NICE_VIEW_HID_TEXT_MAX_LEN - current;
    size_t append_len = MIN((size_t)copy_len, remaining);

    if (append_len > 0) {
        memcpy(buf + current, data + 2, append_len);
        buf[current + append_len] = '\0';
    }

    if (declared_len > append_len) {
        LOG_WRN("Media chunk truncated: declared=%u accepted=%zu", declared_len, append_len);
    }

    if (is_artist) {
        emit_artist();
    } else {
        emit_title();
    }
}

static void process_raw_hid_data(uint8_t *data, uint8_t length) {
    if (length == 0) {
        return;
    }

    LOG_INF("display_process_raw_hid_data - received data_type %u", data[0]);

    // raise disconnect notification after 65 seconds of inactivity
    k_timer_start(&disconnect_timer, K_SECONDS(65), K_NO_WAIT);
    if (!is_connected) {
        LOG_INF("raise_connection_notification: true");
        is_connected = true;
        raise_is_connected_notification((struct is_connected_notification){.value = true});
    }

    uint8_t data_type = data[0];
    switch (data_type) {
    case _TIME:
        if (length < 3) {
            LOG_WRN("Time payload too short (%u)", length);
            break;
        }
        raise_time_notification((struct time_notification){.hour = data[1], .minute = data[2]});
        break;

    case _VOLUME:
        if (length < 2) {
            LOG_WRN("Volume payload too short (%u)", length);
            break;
        }
        last_hid_volume = data[1];

        // debounce volume change events
        if (k_timer_status_get(&volume_timer) > 0 || k_timer_remaining_get(&volume_timer) == 0) {
            k_timer_start(&volume_timer, K_MSEC(150), K_NO_WAIT);
            on_volume_timer(&volume_timer);
        }

        break;

#ifdef CONFIG_NICE_VIEW_HID_SHOW_LAYOUT
    case _LAYOUT:
        if (length < 2) {
            LOG_WRN("Layout payload too short (%u)", length);
            break;
        }
        raise_layout_notification((struct layout_notification){.value = data[1]});
        break;
#endif
    case _MEDIA_ARTIST:
        handle_media_field(true, false, data, length);
        break;
    case _MEDIA_TITLE:
        handle_media_field(false, false, data, length);
        break;
    case _MEDIA_ARTIST_CONT:
        handle_media_field(true, true, data, length);
        break;
    case _MEDIA_TITLE_CONT:
        handle_media_field(false, true, data, length);
        break;
    }
}

static int raw_hid_received_event_listener(const zmk_event_t *eh) {
    struct raw_hid_received_event *event = as_raw_hid_received_event(eh);
    if (event) {
        process_raw_hid_data(event->data, event->length);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(process_raw_hid_event, raw_hid_received_event_listener);
ZMK_SUBSCRIPTION(process_raw_hid_event, raw_hid_received_event);
