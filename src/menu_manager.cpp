#include "menu_manager.h"
#include <stdio.h>

static MenuState s_state = {
    .selectedItem  = MENU_ITEM_DASHBOARD,
    .previousItem  = MENU_ITEM_DASHBOARD,
    .isOpen        = false,
};

static const char* const s_labels[MENU_ITEM_COUNT] = {
    [MENU_ITEM_DASHBOARD]   = "Dashboard",
    [MENU_ITEM_SETTINGS]    = "Settings",
    [MENU_ITEM_SENSORS]     = "Sensors",
    [MENU_ITEM_CAN_STATUS]  = "CAN Status",
    [MENU_ITEM_SPI_STATUS]  = "SPI Status",
};

void menuManager_init(void) {
    s_state.selectedItem = MENU_ITEM_DASHBOARD;
    s_state.isOpen       = false;
}

void menuManager_navigateNext(void) {
    s_state.previousItem = s_state.selectedItem;
    s_state.selectedItem = (MenuItemId)((s_state.selectedItem + 1) % MENU_ITEM_COUNT);
}

void menuManager_navigatePrev(void) {
    s_state.previousItem = s_state.selectedItem;
    s_state.selectedItem = (MenuItemId)
        ((s_state.selectedItem + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT);
}

void menuManager_select(void) {
    s_state.isOpen = true;
    // TODO: trigger LVGL screen transition here
}

void menuManager_back(void) {
    s_state.isOpen = false;
    // TODO: return to parent LVGL screen here
}

MenuState menuManager_getState(void) {
    return s_state;
}

const char* menuManager_getItemLabel(MenuItemId id) {
    if (id >= MENU_ITEM_COUNT) return "???";
    return s_labels[id];
}