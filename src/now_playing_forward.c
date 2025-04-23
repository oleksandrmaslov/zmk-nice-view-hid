#include <zmk/event_manager.h>
#include <raw_hid/raw_hid_events.h>
#include <zmk/split/slave_request.h>

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_EVENT_DECLARE(raw_hid_received_event);

static int forward(const zmk_event_t *eh) {
    const struct raw_hid_received_event *evt = as_raw_hid_received_event(eh);
    if (!evt) { return ZMK_EV_EVENT_BUBBLE; }

    /* wrap in our split channel */
    struct zmk_split_slave_request *req = new_zmk_split_slave_request();
    req->chan = ZMK_SPLIT_CHANNEL_RAW_HID;
    memcpy(req->data, evt->data, evt->data_size);
    req->size = evt->data_size;
    submit_zmk_split_slave_request(req);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(raw_hid_forward, forward);
ZMK_SUBSCRIPTION(raw_hid_forward, raw_hid_received_event);
#endif  // CONFIG_ZMK_SPLIT_ROLE_CENTRAL