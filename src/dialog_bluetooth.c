/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - Bluetooth settings (MAC6100)
 *
 *  Visual shell only: layout and soft-button labels, no BlueZ actions yet.
 */

#include "dialog_bluetooth.h"

#include "buttons.h"
#include "keyboard.h"
#include "events.h"
#include "radio.h"

#include "lvgl/lvgl.h"

#define DIALOG_WIDTH  775
#define DIALOG_HEIGHT 320
#define PARAMS_WIDTH  300

static void construct_cb(lv_obj_t *parent);
static void destruct_cb(void);
static void key_cb(lv_event_t *e);

/* Soft buttons: visible labels, no handlers yet */
static button_data_t btn_on_off = {
    .type  = BTN_TEXT,
    .label = "BT\nOff",
    .press = NULL,
};
static button_data_t btn_scan = {
    .type  = BTN_TEXT,
    .label = "Scan",
    .press = NULL,
};
static button_data_t btn_pair = {
    .type  = BTN_TEXT,
    .label = "Pair",
    .press = NULL,
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

static lv_obj_t *device_table;
static lv_obj_t *label_alias;
static lv_obj_t *label_discoverable;
static lv_obj_t *label_pairable;

static dialog_t dialog = {
    .run          = false,
    .construct_cb = construct_cb,
    .destruct_cb  = destruct_cb,
    .btn_page     = &btn_page,
    .audio_cb     = NULL,
    .key_cb       = key_cb,
};

dialog_t *dialog_bluetooth = &dialog;

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);

    lv_obj_t *cont = lv_obj_create(dialog.obj);
    lv_obj_remove_style(cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(cont, DIALOG_WIDTH, DIALOG_HEIGHT);
    lv_obj_center(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

    /* Left panel - radio identity / adapter status placeholders */
    lv_obj_t *param_cont = lv_obj_create(cont);
    lv_obj_remove_style(param_cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(param_cont, PARAMS_WIDTH, DIALOG_HEIGHT);
    lv_obj_set_flex_flow(param_cont, LV_FLEX_FLOW_COLUMN);

    static lv_style_t style_val_label;
    lv_style_init(&style_val_label);
    lv_style_set_pad_bottom(&style_val_label, 20);
    lv_style_set_pad_left(&style_val_label, 10);

    lv_obj_t *label;

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

    /* Right panel - device list placeholder */
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
    lv_table_set_cell_value(device_table, 0, 0, "No devices - UI shell");

    lv_obj_add_event_cb(device_table, key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(keyboard_group, device_table);
    lv_group_set_editing(keyboard_group, true);
}

static void destruct_cb(void) {
    device_table = NULL;
    label_alias = NULL;
    label_discoverable = NULL;
    label_pairable = NULL;
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
