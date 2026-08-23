/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - Bluetooth (BlueZ D-Bus)
 */

#include "bluetooth.h"

#include "params/params.h"

#include "lvgl/lvgl.h"

#include <gio/gio.h>
#include <string.h>

#define BLUEZ_BUS_NAME  "org.bluez"
#define ADAPTER_PATH    "/org/bluez/hci0"
#define ADAPTER_IFACE   "org.bluez.Adapter1"
#define PROPS_IFACE     "org.freedesktop.DBus.Properties"
#define DBUS_TIMEOUT_MS 3000

static bt_status_t status = BT_STATUS_OFF;
static bool        powered = false;
static bool        discoverable = false;
static bool        pairable = false;
static char        alias[64] = "";

static GDBusConnection *bus = NULL;
static lv_timer_t      *enforce_off_timer = NULL;

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
        g_variant_new("(ss)", ADAPTER_IFACE, name),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, DBUS_TIMEOUT_MS, NULL, &error);

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
        g_variant_new("(ssv)", ADAPTER_IFACE, name, g_variant_new_boolean(value)),
        NULL, G_DBUS_CALL_FLAGS_NONE, DBUS_TIMEOUT_MS, NULL, &error);

    if (!result) {
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    g_variant_unref(result);
    return true;
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

void bluetooth_power_setup(void) {
    /* Default policy: BT off unless user previously enabled it. */
    if (params.bt_enabled.x) {
        bluetooth_power_on();
        return;
    }

    bluetooth_power_off();

    /* hci0 may appear after WiFi RF comes up — re-assert OFF shortly after boot. */
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
    if (!adapter_set_bool("Powered", false)) {
        bluetooth_refresh();
        /* Still remember preference as off. */
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
