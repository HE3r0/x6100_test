/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - Bluetooth (BlueZ D-Bus)
 */

#include "bluetooth.h"

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
    bluetooth_refresh();
    return bluetooth_is_powered();
}

bool bluetooth_power_off(void) {
    if (!adapter_set_bool("Powered", false)) {
        bluetooth_refresh();
        return false;
    }
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
