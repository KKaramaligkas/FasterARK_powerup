# Variants
The `Full` variant (`FasterARK_psp_full.zip`) contains the following extras:
- `DC10`: ultimate recovery tool for PSP (installation and usage: https://github.com/PSP-Arkfive/Despertar-Del-Cementerio).
- `Overclock Stress Tester`: app useful for finding out the maximum overclock that your PSP supports.
- `Custom Launcher`: an Open Source homebrew replacement for the `XMB`.
- `VSH Menu`: a hub for the XMB.
- `Translations`: for both `XMB` and `CL`.
- `LEDA Plugin`: allows running `1.50 homebrew` on any firmware and psp or vita model.
- `ARK-150 Addon`: Only usable on 1K, installs the full 1.50 ARK firmware on memory stick to dual boot with latest firmware (installation and usage: https://github.com/PSP-Arkfive/ARK-150).
- `Plugin Manager`: download, install, update and remove plugins and homebrew over Wi-Fi, like the 3DS's Universal Updater. Also available on its own as `PluginManager.zip`.

# Plugin Manager
- It comes with the `Full` variant, and on a `PSP` the updater (`ARK_UPDATE.zip`) installs or updates it too, keeping its settings and list of installed plugins.
- It also updates ARK itself: open `ARK-5` in the app and choose `Update`. It downloads the updater from the latest release and starts it (from ARK 5.1.6).
- In the `XMB` it's `★ Plugin Manager`, right under `★ Custom Launcher`, and it's listed in the `Custom Launcher` too.
- Installed plugins can also be listed in the `XMB`, in a `Plugins` category that takes the place of the defunct `PlayStation Network` column. It's off by default: turn it on in the app's `Settings`.
- If the `XMB` ever fails to start with the category on, hold `START` while the `XMB` loads, then turn the category off in the app.
- Store format, building and testing: [PluginManager/README.md](PluginManager/README.md).



# Install Instructions

For `ARK` users (any device):
- From ARK 5.1.6 on, the easiest way is the `Plugin Manager`: open `ARK-5` and choose `Update`.
- The XMB's `System Update` and the Custom Launcher's update check are turned off in this build: they downloaded the original ARK-5, which would replace this one.
- Otherwise, download `ARK_UPDATE.zip` and extract.
- Copy `PSP` folder to `Memory Stick` (or `pspemu` folder on `PS Vita`)
- Run the Updater and follow instructions. On a `PSP` it also installs or updates the `Plugin Manager` (`PSP/APPS/PluginManager`).
- `Adrenaline-ARK` users must update to `Adrenaline-8` (Isage's fork).

For `PSP` users on `Official Firmware`:
- Make sure you are on official System Software `6.60` or `6.61` (`6.60TT` and `6.60DT` are also supported), update if necessary.
- Download your prefered variant of `FasterARK_psp.zip`.
- `Extract` the package onto your `Memory Stick`.
- Run `FasterARK` and let it work.
- If all goes well, run `CustomIPL` and install it.

For `PSP` users on older `Custom Firmware`:
- Download the `Full` variant of `FasterARK_psp`.
- Install `DC10`.
- Install `New cIPL` (via `Custom IPL` app).
- Power off device and power back on while holding `L Trigger`.
- `DC10` should boot up, choose the first option (Install 6.61 ARK) and wait for it to finish.
 
For new `PS Vita` users:
- Requires the latest version of `NoPspEmuDrm_mod`: https://github.com/PSP-Arkfive/NoPspEmuDrmArkMod/releases/latest
- Download `FasterARK_psvita.vpk`
- `Install` using VitaShell.
- Run `FasterARK` and choose first option.
- You can also use `Adrenaline-8` to boot into `ARK-5`. 
