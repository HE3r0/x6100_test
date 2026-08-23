/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - Bluetooth (BlueZ D-Bus)
 */

#include "bluetooth.h"

#include "params/params.h"

#include "lvgl/lvgl.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#define BLUEZ_BUS_NAME     "org.bluez"
#define ADAPTER_PATH       "/org/bluez/hci0"
#define ADAPTER_IFACE      "org.bluez.Adapter1"
#define DEVICE_IFACE       "org.bluez.Device1"
#define PROPS_IFACE        "org.freedesktop.DBus.Properties"
#define OBJMGR_IFACE       "org.freedesktop.DBus.ObjectManager"
#define DBUS_TIMEOUT_MS    3000
#define BT_SCAN_TIMEOUT_MS 30000

static bt_status_t status = BT_STATUS_OFF;
static bool        powered = false;
static bool        discoverable = false;
static bool        pairable = false;
static char        alias[64] = "";

static GDBusConnection *bus = NULL;
static lv_timer_t      *enforce_off_timer = NULL;
static lv_timer_t      *scan_timeout_timer = NULL;

static bool             scanning = false;
static bt_device_info_t devices[BT_MAX_DEVICES];
static size_t           device_count = 0;
static int              selected_index = -1;

static bool ensure_bus(void) {
    GError *error = NULL;

    if (bus) {
        return true;
    }

    bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!bus) {
        if (error) {
            g_error_free(error);
        }
        return false;
    }
    return true;
}

static bool adapter_get_property(const char *name, GVariant **out) {
    GError   *error = NULL;
    GVariant *result;

    if (!ensure_bus()) {
        return false;
    }

    result = g_dbus_connection_call_sync(
        bus, BLUEZ_BUS_NAME, ADAPTER_PATH, PROPS_IFACE, "Get",
        g_variant_new("(ss)", ADAPTER_IFACE, name), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, DBUS_TIMEOUT_MS, NULL, &error);

    if (!result) {
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    g_variant_get(result, "(v)", out);
    g_variant_unref(result);
    return true;
}

static bool adapter_set_bool(const char *name, bool value) {
    GError   *error = NULL;
    GVariant *result;

    if (!ensure_bus()) {
        return false;
    }

    result = g_dbus_connection_call_sync(
        bus, BLUEZ_BUS_NAME, ADAPTER_PATH, PROPS_IFACE, "Set",
        g_variant_new("(ssv)", ADAPTER_IFACE, name, g_variant_new_boolean(value)), NULL,
        G_DBUS_CALL_FLAGS_NONE, DBUS_TIMEOUT_MS, NULL, &error);

    if (!result) {
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    g_variant_unref(result);
    return true;
}

static bool adapter_method(const char *method, bool ignore_in_progress) {
    GError   *error = NULL;
    GVariant *result;

    if (!ensure_bus()) {
        return false;
    }

    result = g_dbus_connection_call_sync(
        bus, BLUEZ_BUS_NAME, ADAPTER_PATH, ADAPTER_IFACE, method, NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, DBUS_TIMEOUT_MS, NULL, &error);

    if (!result) {
        bool ok = false;

        if (error) {
            if (ignore_in_progress && error->message && strstr(error->message, "InProgress")) {
                ok = true;
            }
            g_error_free(error);
        }
        return ok;
    }

    g_variant_unref(result);
    return true;
}

static bool prop_get_string(GVariant *props, const char *key, char *buf, size_t buf_size) {
    GVariant *value = g_variant_lookup_value(props, key, G_VARIANT_TYPE_STRING);

    if (!value) {
        return false;
    }
    strncpy(buf, g_variant_get_string(value, NULL), buf_size - 1);
    buf[buf_size - 1] = '\0';
    g_variant_unref(value);
    return true;
}

static bool prop_get_bool(GVariant *props, const char *key, bool *out) {
    GVariant *value = g_variant_lookup_value(props, key, G_VARIANT_TYPE_BOOLEAN);

    if (!value) {
        return false;
    }
    *out = g_variant_get_boolean(value);
    g_variant_unref(value);
    return true;
}

static void prop_get_rssi(GVariant *props, int16_t *out) {
    GVariant *value = g_variant_lookup_value(props, "RSSI", G_VARIANT_TYPE_INT16);

    *out = 0;
    if (!value) {
        return;
    }
    *out = g_variant_get_int16(value);
    g_variant_unref(value);
}

static void sanitize_name(char *name, size_t size) {
    size_t i;

    if (!name || size == 0) {
        return;
    }
    for (i = 0; name[i] != '\0' && i + 1 < size; i++) {
        unsigned char c = (unsigned char)name[i];

        if (c < 0x20 || c > 0x7e) {
            name[i] = '?';
        }
    }
    name[size - 1] = '\0';
    if (name[0] == '\0') {
        strncpy(name, "Unknown", size - 1);
        name[size - 1] = '\0';
    }
}

static int compare_devices(const void *a, const void *b) {
    const bt_device_info_t *da = a;
    const bt_device_info_t *db = b;

    if (da->connected != db->connected) {
        return (int)db->connected - (int)da->connected;
    }
    if (da->paired != db->paired) {
        return (int)db->paired - (int)da->paired;
    }
    return (int)db->rssi - (int)da->rssi;
}

static void parse_objects(GVariant *objects) {
    GVariantIter iter;
    const char  *path = NULL;
    GVariant    *ifaces = NULL;

    device_count = 0;
    g_variant_iter_init(&iter, objects);

    while (device_count < BT_MAX_DEVICES &&
           g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant        *props;
        bt_device_info_t *dev;

        props = g_variant_lookup_value(ifaces, DEVICE_IFACE, G_VARIANT_TYPE("a{sv}"));
        g_variant_unref(ifaces);
        ifaces = NULL;
        if (!props) {
            continue;
        }

        dev = &devices[device_count];
        memset(dev, 0, sizeof(*dev));
        if (path) {
            strncpy(dev->path, path, sizeof(dev->path) - 1);
        }
        if (!prop_get_string(props, "Alias", dev->name, sizeof(dev->name)) &&
            !prop_get_string(props, "Name", dev->name, sizeof(dev->name))) {
            prop_get_string(props, "Address", dev->name, sizeof(dev->name));
        }
        sanitize_name(dev->name, sizeof(dev->name));
        prop_get_string(props, "Address", dev->address, sizeof(dev->address));
        prop_get_bool(props, "Paired", &dev->paired);
        prop_get_bool(props, "Connected", &dev->connected);
        prop_get_rssi(props, &dev->rssi);
        g_variant_unref(props);
        device_count++;
    }

    if (device_count > 1) {
        qsort(devices, device_count, sizeof(devices[0]), compare_devices);
    }
    if (selected_index >= (int)device_count) {
        selected_index = device_count > 0 ? 0 : -1;
    }
}

void bluetooth_refresh(void) {
    GVariant *value = NULL;

    powered = false;
    discoverable = false;
    pairable = false;
    alias[0] = '\0';

    if (!adapter_get_property("Powered", &value)) {
        status = BT_STATUS_ERROR;
        return;
    }

    powered = g_variant_get_boolean(value);
    g_variant_unref(value);
    value = NULL;

    status = powered ? BT_STATUS_ON : BT_STATUS_OFF;

    if (adapter_get_property("Alias", &value)) {
        const char *s = g_variant_get_string(value, NULL);

        if (s) {
            strncpy(alias, s, sizeof(alias) - 1);
            alias[sizeof(alias) - 1] = '\0';
        }
        g_variant_unref(value);
        value = NULL;
    }

    if (adapter_get_property("Discoverable", &value)) {
        discoverable = g_variant_get_boolean(value);
        g_variant_unref(value);
        value = NULL;
    }

    if (adapter_get_property("Pairable", &value)) {
        pairable = g_variant_get_boolean(value);
        g_variant_unref(value);
    }
}

static void enforce_off_timer_cb(lv_timer_t *timer) {
    (void)timer;
    enforce_off_timer = NULL;

    if (params.bt_enabled.x) {
        return;
    }

    bluetooth_refresh();
    if (bluetooth_is_powered() || bluetooth_get_status() == BT_STATUS_ERROR) {
        bluetooth_power_off();
    }
}

static void scan_timeout_cb(lv_timer_t *timer) {
    (void)timer;
    scan_timeout_timer = NULL;
    if (scanning) {
        bluetooth_stop_scan();
    }
}

void bluetooth_power_setup(void) {
    if (params.bt_enabled.x) {
        bluetooth_power_on();
        return;
    }

    bluetooth_power_off();

    if (enforce_off_timer) {
        lv_timer_del(enforce_off_timer);
    }
    enforce_off_timer = lv_timer_create(enforce_off_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(enforce_off_timer, 1);
}

bt_status_t bluetooth_get_status(void) {
    return status;
}

bool bluetooth_is_powered(void) {
    return status == BT_STATUS_ON && powered;
}

bool bluetooth_power_on(void) {
    if (!adapter_set_bool("Powered", true)) {
        bluetooth_refresh();
        return false;
    }
    params_bool_set(&params.bt_enabled, true);
    bluetooth_refresh();
    return bluetooth_is_powered();
}

bool bluetooth_power_off(void) {
    bluetooth_stop_scan();
    device_count = 0;
    selected_index = -1;

    if (!adapter_set_bool("Powered", false)) {
        bluetooth_refresh();
        params_bool_set(&params.bt_enabled, false);
        return false;
    }
    params_bool_set(&params.bt_enabled, false);
    bluetooth_refresh();
    return !bluetooth_is_powered();
}

const char *bluetooth_get_alias(void) {
    return alias;
}

bool bluetooth_is_discoverable(void) {
    return discoverable;
}

bool bluetooth_is_pairable(void) {
    return pairable;
}

bool bluetooth_start_scan(void) {
    if (!bluetooth_is_powered()) {
        return false;
    }
    if (scanning) {
        return true;
    }

    if (!adapter_method("StartDiscovery", true)) {
        return false;
    }

    scanning = true;
    if (scan_timeout_timer) {
        lv_timer_del(scan_timeout_timer);
    }
    scan_timeout_timer = lv_timer_create(scan_timeout_cb, BT_SCAN_TIMEOUT_MS, NULL);
    lv_timer_set_repeat_count(scan_timeout_timer, 1);

    bluetooth_refresh_devices();
    return true;
}

void bluetooth_stop_scan(void) {
    if (scan_timeout_timer) {
        lv_timer_del(scan_timeout_timer);
        scan_timeout_timer = NULL;
    }
    if (!scanning) {
        return;
    }
    adapter_method("StopDiscovery", true);
    scanning = false;
}

bool bluetooth_scanning(void) {
    return scanning;
}

void bluetooth_refresh_devices(void) {
    GError   *error = NULL;
    GVariant *result;
    GVariant *objects;

    if (!ensure_bus()) {
        return;
    }

    result = g_dbus_connection_call_sync(
        bus, BLUEZ_BUS_NAME, "/", OBJMGR_IFACE, "GetManagedObjects", NULL,
        G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, DBUS_TIMEOUT_MS, NULL,
        &error);

    if (!result) {
        if (error) {
            g_error_free(error);
        }
        return;
    }

    objects = g_variant_get_child_value(result, 0);
    parse_objects(objects);
    g_variant_unref(objects);
    g_variant_unref(result);
}

size_t bluetooth_device_count(void) {
    return device_count;
}

size_t bluetooth_copy_devices(bt_device_info_t *out, size_t max) {
    size_t n;

    if (!out || max == 0) {
        return 0;
    }
    n = device_count < max ? device_count : max;
    memcpy(out, devices, n * sizeof(bt_device_info_t));
    return n;
}

void bluetooth_set_selected_index(int index) {
    if (index < 0 || (size_t)index >= device_count) {
        selected_index = -1;
        return;
    }
    selected_index = index;
}

int bluetooth_get_selected_index(void) {
    return selected_index;
}

const bt_device_info_t *bluetooth_get_selected_device(void) {
    if (selected_index < 0 || (size_t)selected_index >= device_count) {
        return NULL;
    }
    return &devices[selected_index];
}
