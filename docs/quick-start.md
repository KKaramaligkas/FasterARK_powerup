# Choose a package and install FasterARK

Download packages from the [FasterARK releases page](https://github.com/KKaramaligkas/FasterARK_powerup/releases). Use one release's files together. For releases with `SHA256SUMS`, compare the downloaded file's SHA-256 with that manifest before copying it to your device.

| Your situation | Package | What it does |
| --- | --- | --- |
| PSP already running ARK | `ARK_UPDATE.zip` | Updates ARK and installs or updates Plugin Manager, preserving its data folder |
| PSP on supported official firmware; core setup | `FasterARK_psp_lite.zip` | Lite installation package |
| PSP on supported official firmware; extras and recovery tools | `FasterARK_psp_full.zip` | Full package, including DC10, Custom Launcher, translations, and Plugin Manager |
| New Vita installation | `FasterARK_psvita.vpk` | Native Vita installer; requires NoPspEmuDrmArkMod |
| ARK installed; lightweight HTTPS browsing | `ARKBrowser.zip` | Web browser under `PSP/GAME/ARKBrowser/`; page layout, forms, TLS 1.2, and downloads |
| ARK installed; Plugin Manager only | `PluginManager.zip` | Standalone app under `PSP/APPS/PluginManager/` |

## Already running ARK

1. If Plugin Manager downloads work, open **ARK-5**, choose **Update**, review replacement files, and start the updater when offered.
2. Otherwise extract `ARK_UPDATE.zip`. Copy its `PSP` folder to the PSP's storage root, or into Vita's `pspemu` folder.
3. Run the updater from the Game column and follow its instructions. Keep the device powered while it runs.

On a PSP with ARK older than 5.1.7, use the archive once; those Plugin Manager versions could not download on real hardware. The XMB System Update and Custom Launcher update check are disabled in this fork; use Plugin Manager or the archive. Adrenaline-ARK users need Isage's Adrenaline-8.

## PSP on official firmware

1. Check **Settings → System Settings → System Information**. Use official 6.60 or 6.61; 6.60TT and 6.60DT are also supported. Update first if necessary.
2. Extract the Lite or Full PSP package to the storage root, preserving its folders.
3. Run **FasterARK** and follow its instructions.
4. After successful installation, run **CustomIPL** to install permanent boot support.

For a PSP on older custom firmware, follow the [DC10 route in the main instructions](../README.md#install-instructions), using the Full package.

## Vita

1. Install [NoPspEmuDrmArkMod](https://github.com/PSP-Arkfive/NoPspEmuDrmArkMod/releases/latest).
2. Install `FasterARK_psvita.vpk` with VitaShell.
3. Run **FasterARK** and choose the first option. Adrenaline-8 can also boot ARK-5.

## Plugin Manager and model differences

| Device | Storage and relevant limitations |
| --- | --- |
| PSP-1000 | Memory Stick; 32 MB RAM; ARK-150 addon is specific to this model |
| PSP-2000 / PSP-3000 | Memory Stick; UMD drive |
| PSP Go | Internal storage or Memory Stick; no UMD drive; test both storage choices |
| PSP Street | Memory Stick and UMD drive; no Wi-Fi, so install packages using a computer |
| Vita / Adrenaline | PSP files live under `pspemu`; no UMD drive; compatibility refers to the emulated PSP system software |

Plugin Manager shows the target's free space and any declared compatibility requirements. Large packages need room for downloads, unpacked files, and recovery copies. Retrying an interrupted download resumes when the server can validate it. Review replacements with Up/Down, then confirm or cancel.

The optional XMB **Plugins** category is off initially. Turn it on in Plugin Manager Settings. If XMB fails to start, hold **START** while XMB loads, then disable the category.

For network and TLS errors, see [Plugin Manager troubleshooting](../PluginManager/README.md#if-it-cant-connect). Release-device validation and screenshots are tracked in the [hardware checklist](hardware-release-checklist.md). The new code still requires physical-device testing.

For direct HTTPS browsing, see the [ARK Browser guide](../Browser/README.md). The Full PSP package includes it; existing installations can use the standalone archive or Plugin Manager entry.
