/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - Bluetooth (BlueZ D-Bus)
 */

#pragma once

#include <stdbool.h>

typedef enum {
    BT_STATUS_OFF = 0,
    BT_STATUS_ON,
    BT_STATUS_ERROR,
} bt_status_t;

/** Apply saved preference: default OFF unless params.bt_enabled. */
void bluetooth_power_setup(void);

void bluetooth_refresh(void);

bt_status_t bluetooth_get_status(void);

bool bluetooth_is_powered(void);

bool bluetooth_power_on(void);

bool bluetooth_power_off(void);

/** Cached adapter alias (empty if unknown). Valid until next refresh/power call. */
const char *bluetooth_get_alias(void);

bool bluetooth_is_discoverable(void);

bool bluetooth_is_pairable(void);
