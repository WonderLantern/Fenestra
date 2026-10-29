#ifndef FENESTRA_PLUGIN_MENU_H
#define FENESTRA_PLUGIN_MENU_H
#include <stdint.h>

/* Trusted, compiled menu contribution. This is not an EL0 pointer ABI.
 * Dynamic card modules will submit copied descriptors through the service ABI. */
typedef struct {
    const char *owner;
    const uint16_t *label;
    uint32_t label_bytes, command;
} FxPluginMenuItem;

extern const FxPluginMenuItem fx_card_test_menu;
#endif
