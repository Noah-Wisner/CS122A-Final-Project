#pragma once
#include <stdint.h>

// Menu item IDs
typedef enum {
    MENU_ITEM_DASHBOARD = 0,
    MENU_ITEM_SETTINGS,
    MENU_ITEM_SENSORS,
    MENU_ITEM_CAN_STATUS,
    MENU_ITEM_SPI_STATUS,
    MENU_ITEM_COUNT  // Keep last
} MenuItemId;

// Menu state
typedef struct {
    MenuItemId  selectedItem;
    MenuItemId  previousItem;
    bool        isOpen;
} MenuState;

void     menuManager_init(void);
void     menuManager_navigateNext(void);
void     menuManager_navigatePrev(void);
void     menuManager_select(void);
void     menuManager_back(void);
MenuState menuManager_getState(void);
const char* menuManager_getItemLabel(MenuItemId id);