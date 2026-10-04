# Hardware release checks

Record results for the exact candidate commit and package checksum. A passing
emulator or host test does not count as a hardware result. Leave unavailable
devices and firmware combinations **Not tested**.

## Candidate

| Field | Value |
| --- | --- |
| Commit | Not tested |
| ARK / Plugin Manager version | Not tested |
| Package / SHA-256 | Not tested |
| Tester / date | Not tested |
| Device model / motherboard | Not tested |
| Firmware (6.60, 6.61, TT, DT, or Vita environment) | Not tested |
| Storage device / capacity / free space | Not tested |
| Network / access point / PSP clock | Not tested |

## Device coverage

Keep one completed copy of this checklist per combination actually tested.
For PSP Go, check both `ms0:` and `ef0:`, including the app running from one
device and installing to the other. PSP Street has no Wi-Fi: test the offline
store, installed-plugin controls, and manual package update route.

| Device | Required coverage | Result |
| --- | --- | --- |
| PSP-1000 | 32 MB memory; supported firmware used by the tester | Not tested |
| PSP-2000 | Installation, Wi-Fi, update, XMB integration | Not tested |
| PSP-3000 | Installation, Wi-Fi, update, XMB integration | Not tested |
| PSP Go | Internal storage, memory stick, and cross-device installation | Not tested |
| PSP Street | Offline use and manual update | Not tested |
| PS Vita | Actual ARK/Adrenaline environment; verify supported features individually | Not tested |

## Checks

| Check | Expected result | Result / evidence |
| --- | --- | --- |
| Start from XMB and Custom Launcher | App opens; controls and installed list work | Not tested |
| First connection and remembered Wi-Fi | Correct network selected; Circle can open network selection | Not tested |
| WLAN switch off and reconnect | Clear status; no freeze or crash | Not tested |
| HTTPS with a correct RTC date | Store, icons, and a package download with certificate checks on | Not tested |
| Incorrect date / refused certificate | Explanation appears; no package is installed | Not tested |
| Large download on PSP-1000 | Completes without exhausting network memory | Not tested |
| Install, update, disable, and uninstall a plugin | Correct files and PLUGINS.TXT lines; unrelated lines survive | Not tested |
| Cancel while downloading or extracting | Installed files and database retain their previous versions | Not tested |
| Cancel while committing package files | Original files, configuration, and database restored | Not tested |
| Insufficient storage | Useful error; prior installation remains usable | Not tested |
| Restart after an interrupted package commit | Recovery completes before the installed list is loaded | Not tested |
| Custom Launcher FTP server with FileZilla's default settings (Quickconnect) | Certificate prompt shows the fingerprint on the PSP's Network screen; browsing, upload and download over TLS; two files at once | Not tested |
| Custom Launcher FTP server: upload speed of a 1 GB ISO, with and without TLS | Faster than the old server's 300 KB/s; note both speeds | Not tested |
| Custom Launcher FTP server: Windows Explorer `ftp://` address | Plain FTP still works | Not tested |
| Storage absent during recovery | Further changes refused; reconnecting permits recovery | Not tested |
| ARK update from Plugin Manager | Correct candidate downloaded; updater starts and completes | Not tested |
| XMB Plugins category enabled | Correct items and names; selecting a plugin opens its details | Not tested |
| XMB Plugins category disabled | Original column returns | Not tested |
| START recovery route | XMB starts without the category/plugins; option can be disabled | Not tested |
| Existing user configuration | Package keep rules and disabled-plugin state survive updates | Not tested |

Use disposable package files and backed-up storage when testing interruptions.
Interrupt only the Plugin Manager's package-file commit. Do not interrupt the
separate ARK Updater while it is writing firmware or IPL; this journal does not
cover those writes.

## Current evidence

The pre-change README records PSP testing on ARK 5.1.6 for the XMB modules,
launching the app, Wi-Fi, and HTTPS with the RTC clock fix. It does not establish
coverage for the new transaction code or every device/firmware combination.
Starting the ARK Updater from the app and the XMB Plugins category were still
listed as needing hardware testing.

The transaction code has host tests for staging, failed writes, cancellation,
and restart recovery, and a GitHub Actions package build. **No hardware result
for these new changes has been recorded.** Add results and evidence here before
describing a release candidate as hardware-tested.

## Flow (formerly ARK Browser) validation (not yet tested on hardware)

Record the exact build, model, firmware, storage device, access point, and page URLs.

- [ ] Launch standalone `Flow.zip` from Game; confirm the CA bundle loads.
- [ ] Install Flow with Plugin Manager on PSP-3000. Record the Plugin Manager version and the last displayed installation stage if it stalls.
- [ ] With ARK Browser 0.3.1 installed and some downloads and cookies in `PSP/GAME/ARKBrowser`, update it to Flow from Plugin Manager. Launch Flow: it reports what it moved, `PSP/GAME/ARKBrowser` is gone (no corrupted data icon in Game), the downloads are in `PSP/GAME/Flow/downloads`, and a site you were signed in to still knows you.
- [ ] Cancel while downloading release checksums and the browser archive, including a server that stops responding; return to the store and retry successfully.
- [ ] Check HOME exit during installation, then relaunch Plugin Manager and confirm recovery completes without changing the previous installation.
- [ ] On PSP-1000, open a text page over verified TLS 1.2 without running out of memory.
- [ ] Open a page with many pictures (for example apache.org) on PSP-1000 and on PSP-2000/3000: pictures appear nearest to the screen first, the status bar counts those still loading, scrolling stays smooth, and text doesn't jump when the page is laid out again. Open a link while pictures load; the next page opens.
- [ ] Open a plain HTTP page; check the unencrypted indicator.
- [ ] Reject expired/untrusted/wrong-host certificates; check clock-error guidance.
- [ ] Reject a server restricted to TLS 1.0/1.1 and an HTTPS-to-HTTP redirect.
- [ ] Enter an address, follow relative links after a redirect, browse the link list, and go back.
- [ ] Cancel a page load; retain the current page and usable controls.
- [ ] Download a file, cancel and retry it, verify the resulting bytes, and preserve an existing download.
- [ ] Check shortened-page / omitted-link notices, and binary file guidance.
- [ ] Confirm PSP Go on both storage devices; check Vita/Adrenaline separately.
- [ ] Check HOME exit during a request, after a request, and while using the keyboard.
- [ ] Capture screenshots of the start page, HTTPS page, link list, and download progress.

### Browser 0.2 (pending physical validation)

- [ ] On PSP-3000, compare CERN and CNN Lite with JavaScript on/off and reload.
- [ ] Open an 8 MB compressed response; cancel during receipt and parsing; verify
      the current page remains visible and `.cache/page.tmp` is removed.
- [ ] Use a local HTTPS test page with modern syntax, module imports, async GET,
      DOMContentLoaded, DOM text mutations, inline/external CSS, and anchors.
- [ ] Confirm colors, bold, variable font size, alignment and underlines; scroll
      all the way to the last line with large fonts.
- [ ] Cancel an infinite loop; exceed script heap limits; verify the app remains
      responsive and available text is shown.
- [ ] Confirm HTTPS rejects HTTP assets and script/GET/module requests reject
      cross-origin targets and redirects. Repeat on Vita/Adrenaline.

### Browser 0.3 (tested in PPSSPP; pending physical validation)

- [ ] Move the pointer with the analog stick across a long page; push past the
      bottom and top edges to scroll; check that scrolling stays smooth.
- [ ] On google.com, find the search box, jump to it with R, type with the
      on-screen keyboard and record what the results page shows.
- [ ] Search from the start page and the Triangle address bar (DuckDuckGo Lite);
      open a result with the pointer and Confirm, and go back.
- [ ] Sign in to a site with a POST form; quit and relaunch, and confirm the
      cookie in `cookies.txt` keeps you signed in. Clear cookies from Start.
- [ ] Use a checkbox, radio buttons, a drop-down list and a reset button.
- [ ] Compare a Wikipedia article in the page view and the reader view (Select).
- [ ] On PSP-1000, open a large page and check memory and layout time.
- [ ] Open hub.docker.com, ubuntu.com and apache.org and compare them with the
      PPSSPP screenshots in pull request #13; time how long ubuntu.com takes.
- [ ] Record a page that lays out wrongly, with its address, for follow-up.
