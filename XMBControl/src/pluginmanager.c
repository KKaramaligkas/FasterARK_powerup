/*
    XmbControl: Plugin Manager integration (FasterARK)

    - "★ Plugin Manager" under "★ Custom Launcher" starts the Plugin Manager
      app (PSP/APPS/PluginManager on the memory stick or internal storage).
    - The PlayStation Network column, whose services were shut down, becomes
      the "Plugins" category: it lists the Plugin Manager and the plugins it
      knows about (data/xmbnames.txt, written by the app). Selecting a plugin
      opens it in the app (data/launch.txt tells the app which one).

    The column is left alone when the app is missing, when the app turned the
    category off (data/noxmbcat), or when START is held while the XMB starts
    (the same button ARK uses to boot without plugins).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspctrl.h>

#include "main.h"
#include "pluginmanager.h"

#define PM_FOLDER "PSP/APPS/PluginManager/"

typedef struct {
    char path[128];
    char title[48];
} PmPlugin;

extern void exec_custom_app(char *path);

static char app_dir[48];        /* "ms0:/PSP/APPS/PluginManager/", empty when missing */
static int app_checked = 0;
static int category = -1;       /* -1 until decided */

static PmPlugin *plugins = NULL;
static int n_plugins = 0;

static SceVshItem *items[PM_MAX_PLUGINS + 1];
static int n_items = 0;
static int in_column = 0;       /* the category's items were added for this column */

static int file_exists(const char *path)
{
    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

static void app_file(char *out, const char *name)
{
    strcpy(out, app_dir);
    strcat(out, name);
}

int pm_app_installed(void)
{
    if (!app_checked) {
        static const char *devices[] = { "ef0:/", "ms0:/" };
        char eboot[64];
        app_checked = 1;
        for (int i = 0; i < 2 && !app_dir[0]; i++) {
            strcpy(eboot, devices[i]);
            strcat(eboot, PM_FOLDER "EBOOT.PBP");
            if (file_exists(eboot)) {
                strcpy(app_dir, devices[i]);
                strcat(app_dir, PM_FOLDER);
            }
        }
    }
    return app_dir[0] != 0;
}

int pm_category_enabled(void)
{
    if (category < 0) {
        category = 0;
        if (pm_app_installed()) {
            char flag[64];
            SceCtrlData pad;
            app_file(flag, "data/noxmbcat");
            memset(&pad, 0, sizeof(pad));
            sceCtrlPeekBufferPositive(&pad, 1);
            category = !file_exists(flag) && !(pad.Buttons & PSP_CTRL_START);
        }
    }
    return category;
}

int pm_is_psn_item(const char *label)
{
    return strcmp(label, "msg_signup") == 0 ||
           strcmp(label, "msg_account_manage") == 0 ||
           strcmp(label, "msg_ps_store") == 0 ||
           strcmp(label, "msg_information_board") == 0;
}

/* copies at most size-1 bytes without cutting a UTF-8 character */
static void copy_title(char *dst, const char *src, int size)
{
    int n = strlen(src);
    if (n > size - 1) {
        n = size - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* reads data/xmbnames.txt: one "path<TAB>title" line per plugin */
static void load_plugins(void)
{
    char index[64];
    n_plugins = 0;
    app_file(index, "data/xmbnames.txt");

    SceUID fd = sceIoOpen(index, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    char *buf = (size > 0 && size <= 16 * 1024) ? malloc(size + 1) : NULL;
    if (buf) size = sceIoRead(fd, buf, size);
    sceIoClose(fd);
    if (!buf) return;
    buf[size > 0 ? size : 0] = 0;

    if (!plugins) plugins = malloc(sizeof(PmPlugin) * PM_MAX_PLUGINS);

    char *line = buf;
    while (plugins && line && *line && n_plugins < PM_MAX_PLUGINS) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = 0;
            char *title = tab + 1;
            char *cr = strchr(title, '\r');
            if (cr) *cr = 0;
            /* plugins deleted since the app last ran are left out */
            if (title[0] && strlen(line) < sizeof(plugins->path) && file_exists(line)) {
                PmPlugin *p = &plugins[n_plugins++];
                strcpy(p->path, line);
                copy_title(p->title, title, sizeof(p->title));
            }
        }
        line = next;
    }
    free(buf);
}

int pm_filter_item(void *a0, int topitem, SceVshItem *item, int (*add)(void *, int, SceVshItem *),
                   void *(*make)(int id, char *label, int action_arg, SceVshItem *icon),
                   SceVshItem *app_icon, SceVshItem *plugin_icon)
{
    if (!pm_is_psn_item(item->text)) {
        in_column = 0;
        return 0;
    }
    if (!pm_category_enabled()) return 0;

    /* the first PlayStation Network item brings in the whole category,
       the other ones are dropped */
    if (!in_column) {
        in_column = 1;
        load_plugins();
        n_items = 0;
        items[n_items++] = make(85, PM_APP_LABEL, sysconf_plugin_manager_arg, app_icon);
        for (int i = 0; i < n_plugins; i++) {
            char label[sizeof(item->text)];
            sprintf(label, PM_PLUGIN_LABEL "%d", i);
            items[n_items++] = make(85, label, sysconf_pm_plugin_arg + i, plugin_icon);
        }
        for (int i = 0; i < n_items; i++) add(a0, topitem, items[i]);
    }
    return 1;
}

const char *pm_plugin_title(const char *label)
{
    if (strncmp(label, PM_PLUGIN_LABEL, sizeof(PM_PLUGIN_LABEL) - 1) != 0) return NULL;
    int i = strtoul(label + sizeof(PM_PLUGIN_LABEL) - 1, NULL, 10);
    return (plugins && i >= 0 && i < n_plugins) ? plugins[i].title : "";
}

void pm_clear_contexts(void)
{
    for (int i = 0; i < n_items; i++) items[i]->context = NULL;
}

int pm_is_action(int action_arg)
{
    return action_arg == sysconf_plugin_manager_arg ||
           (action_arg >= sysconf_pm_plugin_arg && action_arg < sysconf_pm_plugin_arg + PM_MAX_PLUGINS);
}

void pm_execute_action(int action_arg)
{
    char path[64];
    if (!pm_app_installed()) return;

    /* tell the app which plugin to open */
    int plugin = action_arg - sysconf_pm_plugin_arg;
    app_file(path, "data/launch.txt");
    sceIoRemove(path);
    if (action_arg != sysconf_plugin_manager_arg && plugins && plugin >= 0 && plugin < n_plugins) {
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0) {
            sceIoWrite(fd, "plugin ", 7);
            sceIoWrite(fd, plugins[plugin].path, strlen(plugins[plugin].path));
            sceIoWrite(fd, "\n", 1);
            sceIoClose(fd);
        }
    }

    app_file(path, "EBOOT.PBP");
    exec_custom_app(path);
}
