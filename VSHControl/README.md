# VSHControl
Main Custom XMB Driver.

VshCtrl contains:
- All VSH related patches (homebrew boot, custom ps1 boot, etc),
- ISO file display in VSH,
- Launching satelite (vshmenu)

This is ARK-5's VSHControl
([PSP-Arkfive/ark-5](https://github.com/PSP-Arkfive/ark-5), commit `a9b7454`),
unchanged. It's built here because FasterARK's `FLASH0.ARK` in
`Resources/ARK_01234` dates from 16 August 2026, and upstream fixed two things
in this module since then:

- support for XMB themes in EDAT/PTF form (`c045ddd`);
- broken icons of UMD videos (`4029f16`).

Built with the same toolchain, the source of the shipped module (`8194c52`)
reproduces the `ark_vshctrl.prx` in `Resources/ARK_01234/FLASH0.ARK`. Only the
flags of a few kernel imports differ (0x0009 instead of 0x0001), which the
current SDK sets, as the shipped module already did for its other imports.

The top-level `make flash0` builds the module, wraps it with psp-cfw-sdk's
`pspgz.py` like ARK does, and puts it in `build/FLASH0.ARK` together with
XMBControl (`tools/flash0.py`). When upstream updates the binaries in
`Resources/ARK_01234/FLASH0.ARK`, this folder should be updated to the same
commit, or removed.
