/*
    XmbControl: Plugin Manager integration (FasterARK)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef XMBCTRL_PLUGINMANAGER_H
#define XMBCTRL_PLUGINMANAGER_H

#include "main.h"

/* most plugins listed in the XMB "Plugins" category */
#define PM_MAX_PLUGINS 32

/* XMB labels: the "★ Plugin Manager" items, the category name (for
   translations) and the plugins of the category (followed by a number) */
#define PM_APP_LABEL "xmbmsgtop_plugin_manager"
#define PM_CATEGORY_LABEL "xmbmsg_plugins_category"
#define PM_PLUGIN_LABEL "xmbmsg_pm_plugin_"

/* 1 when the Plugin Manager app is on the memory stick or internal storage */
int pm_app_installed(void);

/* 1 when the PlayStation Network column becomes the "Plugins" category */
int pm_category_enabled(void);

/* 1 for the items of the PlayStation Network column */
int pm_is_psn_item(const char *label);

/* Called for every item the XMB adds. Returns 1 when the item is a
   PlayStation Network item replaced by the "Plugins" category (the first one
   adds the category's items with add/make, then none must be added). */
int pm_filter_item(void *a0, int topitem, SceVshItem *item, int (*add)(void *, int, SceVshItem *),
                   void *(*make)(int id, char *label, int action_arg, SceVshItem *icon),
                   SceVshItem *app_icon, SceVshItem *plugin_icon);

/* Title of a plugin of the category, NULL when the label is not one */
const char *pm_plugin_title(const char *label);

/* Clears the context menu of the category's items */
void pm_clear_contexts(void);

/* Actions of the "★ Plugin Manager" items and of the category's plugins:
   they start the app, opening the plugin when there is one. */
int pm_is_action(int action_arg);
void pm_execute_action(int action_arg);

#endif
