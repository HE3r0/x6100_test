/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - Bluetooth (BlueZ D-Bus)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BT_STATUS_OFF = 0,
    BT_STATUS_ON,
    BT_STATUS_ERROR,
} bt_status_t;

#define BT_MAX_DEVICES 32

typedef struct {
    char    path[128];
    char    name[64];
    char    address[18];
    int16_t rssi;
    bool    paired;
    bool    connected;
} bt_device_info_t;

void bluetooth_power_setup(void);

void bluetooth_refresh(void);

bt_status_t bluetooth_get_status(void);

bool bluetooth_is_powered(void);

bool bluetooth_power_on(void);

bool bluetooth_power_off(void);

const char *bluetooth_get_alias(void);

bool bluetooth_is_discoverable(void);

bool bluetooth_is_pairable(void);

bool bluetooth_start_scan(void);

void bluetooth_stop_scan(void);

bool bluetooth_scanning(void);

void bluetooth_refresh_devices(void);

size_t bluetooth_device_count(void);

size_t bluetooth_copy_devices(bt_device_info_t *out, size_t max);

void bluetooth_set_selected_index(int index);

int bluetooth_get_selected_index(void);

const bt_device_info_t *bluetooth_get_selected_device(void);
