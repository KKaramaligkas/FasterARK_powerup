# Custom Launcher (VBOOT.PBP) patches

`Resources/Extras/VBOOT.PBP` is built from
[PSP-Arkfive/Launcher-Menus](https://github.com/PSP-Arkfive/Launcher-Menus) `arkMenu`
at commit `2457b08` with `theme-switch-fixes.patch` applied.

To rebuild (same toolchain as the Launcher-Menus CI: latest pspdev + psp-cfw-sdk + ya2d):

    git clone https://github.com/PSP-Arkfive/Launcher-Menus
    cd Launcher-Menus && git checkout 2457b08
    git apply /path/to/theme-switch-fixes.patch
    make -C arkMenu
    cp arkMenu/EBOOT.PBP /path/to/FasterARK/Resources/Extras/VBOOT.PBP

## theme-switch-fixes.patch

Fixes crashes and hangs when previewing, installing or cancelling a theme,
and when playing a video from the file browser (which unloads and reloads
the theme the same way):

- The game list entries, the browser's icons and the icon/scan thread kept
  pointers to the old theme's images after they were freed; the icon thread
  could even rebuild the game list with the old images while they were being
  deleted. The theme swap now stops that thread, drops those references,
  swaps the theme, then restarts the thread.
- The menu sound (played when the theme file is selected) was freed while
  still playing. Deleting an `MP3` now stops its playback first.
- A truncated or corrupt `THEME.ARK` made the package parser loop forever,
  and long entry names overflowed its name buffer.
- A theme missing a required file, or whose required image is not a PNG
  (e.g. a renamed JPEG, which loads as a NULL texture that gets
  dereferenced), is refused up front instead of freeing the current theme.
  The same goes for a theme that would leave the launcher without a
  background.
- `Image` constructors left the texture pointer uninitialized for data in an
  unexpected format (e.g. an Exif `DEFBG.JPG`), and the fallback to the
  NOICON texture crashed while a theme was being loaded (NOICON not loaded
  yet), e.g. with an undecodable custom `BG.JPG`.
- Cancelling the first prompt no longer reloads the theme for nothing.

With the same toolchain, the rebuilt `VBOOT.PBP` is reproducible: a clean clone at `2457b08` with this
patch applied builds a byte-identical file.
