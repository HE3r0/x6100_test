/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - Bluetooth settings (MAC6100)
 */

#include "dialog_bluetooth.h"

#include "bluetooth.h"
#include "buttons.h"
#include "events.h"
#include "keyboard.h"
#include "msg.h"
#include "radio.h"

#include "lvgl/lvgl.h"

#include <stdint.h>
#include <stdio.h>

#define DIALOG_WIDTH  775
#define DIALOG_HEIGHT 320
#define PARAMS_WIDTH  300
#define UI_POLL_MS    1000

static void construct_cb(lv_obj_t *parent);
static void destruct_cb(void);
static void key_cb(lv_event_t *e);
static void cell_selected_cb(lv_event_t *e);

static void bt_power_toggle_cb(button_data_t *btn_data);
static void bt_scan_cb(button_data_t *btn_data);
static const char *bt_on_off_label_getter(void);
static const char *bt_scan_label_getter(void);
static const char *bt_pair_label_getter(void);

static void refresh_status_labels(void);
static void refresh_soft_buttons(void);
static void refresh_device_table(void);
static void ui_poll_cb(lv_timer_t *timer);

static button_data_t btn_on_off = {
    .type     = BTN_TEXT_FN,
    .label_fn = bt_on_off_label_getter,
    .press    = bt_power_toggle_cb,
};
static button_data_t btn_scan = {
    .type     = BTN_TEXT_FN,
    .label_fn = bt_scan_label_getter,
    .press    = bt_scan_cb,
};
static button_data_t btn_pair = {
    .type     = BTN_TEXT_FN,
    .label_fn = bt_pair_label_getter,
    .press    = NULL,
};

static buttons_page_t btn_page = {
    {
     &btn_on_off,
     &btn_scan,
     &btn_pair,
     NULL,
     NULL,
     }
};

static lv_obj_t   *device_table = NULL;
static lv_obj_t   *label_alias = NULL;
static lv_obj_t   *label_discoverable = NULL;
static lv_obj_t   *label_pairable = NULL;
static lv_timer_t *ui_poll_timer = NULL;
static bool        updating_table = false;

static dialog_t dialog = {
    .run          = false,
    .construct_cb = construct_cb,
    .destruct_cb  = destruct_cb,
    .btn_page     = &btn_page,
    .audio_cb     = NULL,
    .key_cb       = key_cb,
};

dialog_t *dialog_bluetooth = &dialog;

static void refresh_status_labels(void) {
    const char *name;

    if (!label_alias) {
        return;
    }

    name = bluetooth_get_alias();
    if (name && name[0]) {
        lv_label_set_text(label_alias, name);
    } else if (bluetooth_get_status() == BT_STATUS_ERROR) {
        lv_label_set_text(label_alias, "(unavailable)");
    } else {
        lv_label_set_text(label_alias, "-");
    }

    if (bluetooth_get_status() == BT_STATUS_ERROR) {
        lv_label_set_text(label_discoverable, "-");
        lv_label_set_text(label_pairable, "-");
        return;
    }

    lv_label_set_text(label_discoverable, bluetooth_is_discoverable() ? "yes" : "no");
    lv_label_set_text(label_pairable, bluetooth_is_pairable() ? "yes" : "no");
}

static void refresh_soft_buttons(void) {
    buttons_page_t *page = buttons_get_cur_page();
    size_t          i;

    if (!page) {
        return;
    }

    for (i = 0; i < BUTTONS; i++) {
        if (page->items[i]) {
            buttons_refresh(page->items[i]);
        }
    }
}

static void refresh_device_table(void) {
    bt_device_info_t devices[BT_MAX_DEVICES];
    size_t           count;
    size_t           i;
    char             line[96];

    if (!device_table) {
        return;
    }

    updating_table = true;

    if (!bluetooth_is_powered()) {
        lv_table_set_row_cnt(device_table, 1);
        lv_table_set_cell_value(device_table, 0, 0, "Turn Bluetooth on");
        lv_table_set_cell_user_data(device_table, 0, 0, (void *)(intptr_t)-1);
        updating_table = false;
        return;
    }

    if (bluetooth_scanning()) {
        bluetooth_refresh_devices();
    }

    count = bluetooth_copy_devices(devices, BT_MAX_DEVICES);
    if (count == 0) {
        lv_table_set_row_cnt(device_table, 1);
        lv_table_set_cell_value(device_table, 0, 0,
                                bluetooth_scanning() ? "Scanning..." : "Press Scan");
        lv_table_set_cell_user_data(device_table, 0, 0, (void *)(intptr_t)-1);
        updating_table = false;
        return;
    }

    lv_table_set_row_cnt(device_table, (uint16_t)count);
    for (i = 0; i < count; i++) {
        const char *mark = devices[i].connected ? " [C]" : (devices[i].paired ? " [P]" : "");

        if (devices[i].rssi != 0) {
            snprintf(line, sizeof(line), "%s%s  %s  %ddBm", devices[i].name, mark,
                     devices[i].address, (int)devices[i].rssi);
        } else {
            snprintf(line, sizeof(line), "%s%s  %s", devices[i].name, mark, devices[i].address);
        }
        lv_table_set_cell_value(device_table, (uint16_t)i, 0, line);
        lv_table_set_cell_user_data(device_table, (uint16_t)i, 0, (void *)(intptr_t)i);
    }

    updating_table = false;
}

static void ui_poll_cb(lv_timer_t *timer) {
    (void)timer;

    if (!dialog.run) {
        return;
    }

    if (bluetooth_scanning()) {
        refresh_device_table();
        refresh_soft_buttons();
    }
}

static void construct_cb(lv_obj_t *parent) {
    lv_obj_t *cont;
    lv_obj_t *param_cont;
    lv_obj_t *label;

    dialog.obj = dialog_init(parent);

    cont = lv_obj_create(dialog.obj);
    lv_obj_remove_style(cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(cont, DIALOG_WIDTH, DIALOG_HEIGHT);
    lv_obj_center(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

    param_cont = lv_obj_create(cont);
    lv_obj_remove_style(param_cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(param_cont, PARAMS_WIDTH, DIALOG_HEIGHT);
    lv_obj_set_flex_flow(param_cont, LV_FLEX_FLOW_COLUMN);

    static lv_style_t style_val_label;
    lv_style_init(&style_val_label);
    lv_style_set_pad_bottom(&style_val_label, 20);
    lv_style_set_pad_left(&style_val_label, 10);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Name (alias):");
    label_alias = lv_label_create(param_cont);
    lv_label_set_text(label_alias, "-");
    lv_obj_add_style(label_alias, &style_val_label, 0);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Discoverable:");
    label_discoverable = lv_label_create(param_cont);
    lv_label_set_text(label_discoverable, "-");
    lv_obj_add_style(label_discoverable, &style_val_label, 0);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Pairable:");
    label_pairable = lv_label_create(param_cont);
    lv_label_set_text(label_pairable, "-");
    lv_obj_add_style(label_pairable, &style_val_label, 0);

    device_table = lv_table_create(cont);
    lv_table_set_col_cnt(device_table, 1);
    lv_table_set_col_width(device_table, 0, DIALOG_WIDTH - PARAMS_WIDTH - 2);
    lv_obj_set_height(device_table, DIALOG_HEIGHT);
    lv_obj_center(device_table);
    lv_obj_set_flex_grow(device_table, 1);

    lv_obj_remove_style(device_table, NULL, LV_PART_MAIN | LV_PART_ITEMS | LV_STATE_ANY);
    lv_obj_set_style_bg_opa(device_table, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(device_table, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(device_table, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(device_table, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(device_table, 128, LV_PART_MAIN);

    lv_obj_set_style_border_width(device_table, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(device_table, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(device_table, lv_color_white(), LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_opa(device_table, LV_OPA_30, LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_pad_top(device_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(device_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(device_table, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(device_table, 0, LV_PART_ITEMS);

    lv_table_set_row_cnt(device_table, 1);
    lv_table_set_cell_value(device_table, 0, 0, "Press Scan");
    lv_table_set_cell_user_data(device_table, 0, 0, (void *)(intptr_t)-1);

    lv_obj_add_event_cb(device_table, key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(device_table, cell_selected_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(keyboard_group, device_table);
    lv_group_set_editing(keyboard_group, true);

    bluetooth_refresh();
    refresh_status_labels();
    refresh_device_table();
    refresh_soft_buttons();

    ui_poll_timer = lv_timer_create(ui_poll_cb, UI_POLL_MS, NULL);
}

static void destruct_cb(void) {
    if (ui_poll_timer) {
        lv_timer_del(ui_poll_timer);
        ui_poll_timer = NULL;
    }
    bluetooth_stop_scan();

    device_table = NULL;
    label_alias = NULL;
    label_discoverable = NULL;
    label_pairable = NULL;
}

static void cell_selected_cb(lv_event_t *e) {
    lv_obj_t *obj;
    uint16_t  row;
    uint16_t  col;
    intptr_t  index;

    if (updating_table) {
        return;
    }

    obj = lv_event_get_target(e);
    lv_table_get_selected_cell(obj, &row, &col);
    if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE) {
        bluetooth_set_selected_index(-1);
        refresh_soft_buttons();
        return;
    }

    index = (intptr_t)lv_table_get_cell_user_data(obj, row, col);
    bluetooth_set_selected_index((int)index);
    refresh_soft_buttons();
}

static void bt_power_toggle_cb(button_data_t *btn_data) {
    (void)btn_data;

    bluetooth_refresh();

    if (bluetooth_is_powered()) {
        if (bluetooth_power_off()) {
            msg_update_text_fmt("Bluetooth off");
        } else {
            msg_update_text_fmt("Bluetooth power off failed");
        }
    } else {
        if (bluetooth_power_on()) {
            msg_update_text_fmt("Bluetooth on");
        } else {
            msg_update_text_fmt("Bluetooth power on failed");
        }
    }

    refresh_status_labels();
    refresh_device_table();
    refresh_soft_buttons();
}

static void bt_scan_cb(button_data_t *btn_data) {
    (void)btn_data;

    if (!bluetooth_is_powered()) {
        msg_update_text_fmt("Turn Bluetooth on first");
        return;
    }

    if (bluetooth_scanning()) {
        bluetooth_stop_scan();
        msg_update_text_fmt("Scan stopped");
    } else {
        if (bluetooth_start_scan()) {
            msg_update_text_fmt("Scanning...");
        } else {
            msg_update_text_fmt("Scan failed");
        }
    }

    refresh_device_table();
    refresh_soft_buttons();
}

static const char *bt_on_off_label_getter(void) {
    switch (bluetooth_get_status()) {
    case BT_STATUS_ON:
        return "BT\nOn";
    case BT_STATUS_ERROR:
        return "BT\nErr";
    case BT_STATUS_OFF:
    default:
        return "BT\nOff";
    }
}

static const char *bt_scan_label_getter(void) {
    if (!bluetooth_is_powered()) {
        return "";
    }
    return bluetooth_scanning() ? "Stop\nScan" : "Scan";
}

static const char *bt_pair_label_getter(void) {
    if (!bluetooth_is_powered()) {
        return "";
    }
    return "Pair";
}

static void key_cb(lv_event_t *e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
    case LV_KEY_ESC:
        dialog_destruct();
        break;

    case KEY_VOL_LEFT_EDIT:
    case KEY_VOL_LEFT_SELECT:
        radio_change_vol(-1);
        break;

    case KEY_VOL_RIGHT_EDIT:
    case KEY_VOL_RIGHT_SELECT:
        radio_change_vol(1);
        break;
    }
}
