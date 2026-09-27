# XMBControl
Extended Custom XMB Driver.

This is ARK-5's XMBControl
([PSP-Arkfive/ark-5](https://github.com/PSP-Arkfive/ark-5), commit `a9b7454`)
with the XMB side of FasterARK's [Plugin Manager](../PluginManager):

- **★ Plugin Manager** right under **★ Custom Launcher**, when
  `PSP/APPS/PluginManager/EBOOT.PBP` exists on the memory stick or internal
  storage.
- The **Plugins** category. The PlayStation Network column is renamed and its
  items are replaced by the Plugin Manager and the plugins it lists in
  `data/xmbnames.txt`. Picking a plugin writes `data/launch.txt` and starts the
  app on that plugin's page.
  - The column is left alone when the app is missing, when the app turned the
    category off (`data/noxmbcat`), or when START is held while the XMB
    starts.
  - The category name can be translated with the `xmbmsg_plugins_category`
    key in `lang_*.json`.
- Translation lookups skip the `NULL` placeholder of the plugin install
  options instead of passing it to `strcmp`.

The changes are in `src/pluginmanager.c`, `include/pluginmanager.h`, and small
hooks in `src/xmbpatch.c` and `include/main.h`. Without them this folder is
identical to upstream: built with the same toolchain, the untouched source
produces the `ark_xmbctrl.prx` shipped in `Resources/ARK_01234/FLASH0.ARK`,
apart from the build date.

The top-level `make flash0` builds the module, wraps it with psp-cfw-sdk's
`pspgz.py` like ARK does, and writes `build/FLASH0.ARK` (`tools/flash0.py`).
Every package uses that `FLASH0.ARK`.
