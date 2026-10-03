.PHONY: psp vita updater translations pluginmanager browser launcher flash0 version

PY = $(shell which python3)
PSPDEV = $(shell psp-config --pspdev-path)
BUILDTOOLS = $(PSPDEV)/share/psp-cfw-sdk/build-tools
CHOVYSIGNDIR = ./Resources/Chovy-Sign
CHOVYSIGN = $(CHOVYSIGNDIR)/ChovySign-CLI
LANGFOLDER = Resources/Language/Translations/resources
FLASH0 = build/FLASH0.ARK
VERSION_TXT = build/VERSION.TXT

all: translations themes pluginmanager browser launcher flash0 psp vita updater
	echo "All Done!"

# Plugin Manager app (PSP/APPS/PluginManager), also released on its own for
# the store's self-update and for installs without the Full variant
pluginmanager:
	$(PY) tools/release_store.py --seed
	make -C PluginManager package
	mkdir -p dist
	cp PluginManager/dist/PluginManager.zip dist/

# Standalone lightweight HTTPS browser, also included in the Full PSP package.
browser:
	make -C Browser package
	mkdir -p dist
	cp Browser/dist/ARKBrowser.zip dist/

# ARK's Custom Launcher (ARK_01234/VBOOT.PBP), built from Launcher/ for its
# FTP server with TLS (Launcher/ftpd); see Launcher/README.md
launcher:
	make -C Launcher
	mkdir -p build
	cp Launcher/EBOOT.PBP build/VBOOT.PBP

# ARK's FLASH0.ARK with two modules built here: XMBControl from XMBControl/
# (Plugin Manager entry under Custom Launcher and the XMB "Plugins" category)
# and VSHControl from VSHControl/ (upstream fixes newer than the binaries)
flash0:
	make -C XMBControl
	make -C VSHControl
	mkdir -p build
	$(PY) $(BUILDTOOLS)/gz/pspgz.py build/ark_xmbctrl.prx $(BUILDTOOLS)/gz/UserModule.hdr XMBControl/xmbctrl.prx XmbControl 0x0000
	$(PY) $(BUILDTOOLS)/gz/pspgz.py build/ark_vshctrl.prx $(BUILDTOOLS)/gz/SystemControl.hdr VSHControl/vshctrl.prx VshControl 0x3007
	$(PY) tools/flash0.py replace Resources/ARK_01234/FLASH0.ARK $(FLASH0) \
		/kd/ark_xmbctrl.prx=build/ark_xmbctrl.prx /kd/ark_vshctrl.prx=build/ark_vshctrl.prx

# ARK_01234/VERSION.TXT: the release version (Updater/version.h), which the
# Plugin Manager compares with its store to offer ARK updates
version:
	mkdir -p build
	awk '/define ARK_MAJOR_VERSION/ {a=$$3} /define ARK_MINOR_VERSION/ {b=$$3} /define ARK_MICRO_VERSION/ {c=$$3} \
		END {if (a == "" || b == "" || c == "") exit 1; printf "%s.%s.%s\r\n", a, b, c}' Updater/version.h > $(VERSION_TXT)
	$(PY) tools/check_store_version.py $(VERSION_TXT) PluginManager/store/store.json

translations:
	$(PY) $(BUILDTOOLS)/pftools/bdf_to_pf.py $(LANGFOLDER)/satelite_chs_utf8.txt Resources/Language/quan.bdf $(LANGFOLDER)/satelite_chs.txt $(LANGFOLDER)/CHS.pf
	$(PY) $(BUILDTOOLS)/pftools/bdf_to_pf.py $(LANGFOLDER)/satelite_cht_utf8.txt Resources/Language/quan.bdf $(LANGFOLDER)/satelite_cht.txt $(LANGFOLDER)/CHT.pf
	$(PY) $(BUILDTOOLS)/pftools/bdf_to_pf.py $(LANGFOLDER)/satelite_jp_utf8.txt Resources/Language/quan.bdf $(LANGFOLDER)/satelite_jp.txt $(LANGFOLDER)/JP.pf
	$(PY) $(BUILDTOOLS)/pftools/bdf_to_pf.py $(LANGFOLDER)/satelite_kr_utf8.txt Resources/Language/quan.bdf $(LANGFOLDER)/satelite_kr.txt $(LANGFOLDER)/KR.pf
	$(PY) $(BUILDTOOLS)/pack/pkg-res.py Resources/Language LANG.ARK

themes:
	mkdir -p dist/tmp/
	cp -r Resources/themes dist/tmp/
	cd dist/tmp/ && zip -m -r themes.zip * && cd ../../ && mv dist/tmp/themes.zip dist/
	rm -rf dist/tmp

psp: translations pluginmanager browser launcher flash0 version
	make -C PSP
#	PSP Lite Install
	mkdir -p dist/tmp/PSP/GAME/FasterARK/
	mkdir -p dist/tmp/PSP/SAVEDATA/
	cp -r Resources/ARK_01234 dist/tmp/PSP/SAVEDATA/
	cp $(FLASH0) dist/tmp/PSP/SAVEDATA/ARK_01234/
	cp $(VERSION_TXT) dist/tmp/PSP/SAVEDATA/ARK_01234/
	cp -r Resources/CustomIPL dist/tmp/PSP/GAME/
	cp PSP/EBOOT.PBP dist/tmp/PSP/GAME/FasterARK/
	cp Resources/LIBS/ipl_update.prx dist/tmp/PSP/GAME/CustomIPL/
	cp Resources/LIBS/kbooti_update.prx dist/tmp/PSP/GAME/CustomIPL/
	cp Resources/LIBS/kpspident.prx dist/tmp/PSP/GAME/CustomIPL/
	cp Resources/LIBS/usbdevice.prx dist/tmp/PSP/GAME/FasterARK/
	cp Resources/LIBS/idsregeneration.prx dist/tmp/PSP/GAME/FasterARK/
	cd dist/tmp/ && zip -m -r FasterARK_psp_lite.zip * && cd ../../ && mv dist/tmp/FasterARK_psp_lite.zip dist/
#	PSP Full Install
	mkdir -p dist/tmp/PSP/GAME/FasterARK/
	mkdir -p dist/tmp/PSP/SAVEDATA/
	mkdir -p dist/tmp/PSP/APPS/
	cp -r Resources/ARK_01234 dist/tmp/PSP/SAVEDATA/
	cp $(FLASH0) dist/tmp/PSP/SAVEDATA/ARK_01234/
	cp $(VERSION_TXT) dist/tmp/PSP/SAVEDATA/ARK_01234/
	cp Resources/Extras/* dist/tmp/PSP/SAVEDATA/ARK_01234/
	cp build/VBOOT.PBP dist/tmp/PSP/SAVEDATA/ARK_01234/
	cp -r PluginManager/dist/PSP/APPS/PluginManager dist/tmp/PSP/APPS/
	cp -r Browser/dist/PSP/GAME/ARKBrowser dist/tmp/PSP/GAME/
	cp Resources/Language/Translations/LANG.ARK dist/tmp/PSP/SAVEDATA/ARK_01234/
	cp -r Resources/CustomIPL dist/tmp/PSP/GAME/
	cp -r Resources/DC10 dist/tmp/PSP/GAME/
	cp -r Resources/ARK150on660 dist/tmp/PSP/GAME/
	cp -r Resources/OC_Tester dist/tmp/PSP/GAME/
	cp PSP/EBOOT.PBP dist/tmp/PSP/GAME/FasterARK/
	cp Resources/LIBS/usbdevice.prx dist/tmp/PSP/GAME/FasterARK/
	cp Resources/LIBS/idsregeneration.prx dist/tmp/PSP/GAME/FasterARK/
	cp Resources/LIBS/ipl_update.prx dist/tmp/PSP/GAME/CustomIPL/
	cp Resources/LIBS/kbooti_update.prx dist/tmp/PSP/GAME/CustomIPL/
	cp Resources/LIBS/kpspident.prx dist/tmp/PSP/GAME/CustomIPL/
	cp Resources/LIBS/iop.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/ipl_update.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/usbdevice.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/idsregeneration.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/vlf.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/intraFont-vlf.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/pspdecrypt.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/libpsardumper.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/lflash_fdisk.prx dist/tmp/PSP/GAME/DC10/
	cp Resources/LIBS/pspdecrypt.prx dist/tmp/PSP/GAME/ARK150on660/
	cp Resources/LIBS/libpsardumper.prx dist/tmp/PSP/GAME/ARK150on660/
	cd dist/tmp/ && zip -m -r FasterARK_psp_full.zip * && cd ../../ && mv dist/tmp/FasterARK_psp_full.zip dist/
	rm -r dist/tmp/

vita: translations launcher flash0 version
	mkdir -p dist
	mkdir -p PSVita/res/save
	mkdir -p PSVita/loader/psp/eboot/iso_files/psp_game/sysdir
	mkdir -p PSVita/res/psp/
	mkdir -p PSVita/res/rif
	mkdir -p PSVita/res/psx/
	cp -r Resources/ARK_01234 PSVita/res/save/
	cp $(FLASH0) PSVita/res/save/ARK_01234/
	cp $(VERSION_TXT) PSVita/res/save/ARK_01234/
	cp -r Resources/Extras/* PSVita/res/save/ARK_01234/
	cp build/VBOOT.PBP PSVita/res/save/ARK_01234/
	cp -r Resources/PSVita/* PSVita/res/save/ARK_01234/
	cp Resources/Language/Translations/LANG.ARK PSVita/res/save/ARK_01234/
#   ePSP Bubble
	cp Resources/ARK_01234/ICON0.PNG PSVita/loader/psp/eboot/iso_files/psp_game/
	make -C PSVita/loader/psp/eboot
	cp PSVita/loader/psp/eboot/psploader.prx PSVita/loader/psp/eboot/iso_files/psp_game/sysdir/boot.bin
	mkisofs -o PSVita/loader/psp/eboot/psploader.iso PSVita/loader/psp/eboot/iso_files/
	$(CHOVYSIGN) --psp PSVita/loader/psp/eboot/psploader.iso --no-psvimg --nopspemudrm EP0099-NPUZ01234_00-CHOVYSIGN0000000
	cp $(CHOVYSIGNDIR)/output/PSP/LICENSE/EP0099-NPUZ01234_00-CHOVYSIGN0000000.rif PSVita/res/rif/psp.rif
	cp $(CHOVYSIGNDIR)/output/PSP/GAME/NPUZ01234/EBOOT.PBP PSVita/res/psp/
	cp Resources/ARK_01234/ICON0.PNG PSVita/loader/psp/pboot/
	make -C PSVita/loader/psp/pboot
	cp PSVita/loader/psp/pboot/PBOOT.PBP PSVita/res/psp/
	make -C Resources/Peops
	$(PY) $(BUILDTOOLS)/gz/pspgz.py PSVita/res/save/ARK_01234/PS1SPU.PRX $(BUILDTOOLS)/gz/UserModule.hdr Resources/Peops/peops.prx peops 0x0000
#   ePSX Bubble
	make -C PSVita/loader/psx
	make -C PSVita/loader/psx/payload
	cp PSVita/loader/psx/payload/ARKX.BIN PSVita/res/psx/
	$(CHOVYSIGN) --pops PSVita/loader/psx/psxloader.cue --pops-info "ARK-X" PSVita/loader/psx/ICON0.PNG --pops-eboot PSVita/loader/psx/psxloader.prx --no-psvimg --nopspemudrm EP0099-SCPS10084_00-CHOVYSIGN0000000
	cp $(CHOVYSIGNDIR)/output/PSP/LICENSE/EP0099-SCPS10084_00-CHOVYSIGN0000000.rif PSVita/res/rif/psx.rif
	cp $(CHOVYSIGNDIR)/output/PSP/GAME/SCPS10084/EBOOT.PBP PSVita/res/psx/
#	Installer
	mkdir -p PSVita/build
	cd PSVita/build && cmake .. && make && cd ../../
	cp PSVita/build/FasterARK.vpk dist/FasterARK_psvita.vpk

updater: translations pluginmanager launcher flash0 version
#	Updater
	mkdir -p dist/tmp/
	cp -r Resources/LIBS Updater/Resources/
	cp -r Resources/ARK_01234 Updater/Resources/
#	the updater also installs the Plugin Manager app (PSP/APPS/PluginManager)
	mkdir -p Updater/Resources/APPS
	cp -r PluginManager/dist/PSP/APPS/PluginManager Updater/Resources/APPS/
	cp $(FLASH0) Updater/Resources/ARK_01234/
	cp $(VERSION_TXT) Updater/Resources/ARK_01234/
	cp Resources/DC10/DC10.ARK Updater/Resources/ARK_01234/
	cp Resources/ARK150on660/FLASH150.ARK Updater/Resources/ARK_01234/
	cp Resources/ARK150on660/LANG150.ARK Updater/Resources/ARK_01234/
	cp Resources/Language/Translations/LANG.ARK Updater/Resources/ARK_01234/
	cp build/VBOOT.PBP Updater/Resources/ARK_01234/
	cp Resources/Extras/VSHMENU.PRX Updater/Resources/ARK_01234/
	cp Resources/PSVita/XBOOT.PBP Updater/Resources/ARK_01234/
	make -C Resources/Peops
	$(PY) $(BUILDTOOLS)/gz/pspgz.py Updater/Resources/ARK_01234/PS1SPU.PRX $(BUILDTOOLS)/gz/UserModule.hdr Resources/Peops/peops.prx peops 0x0000
	make -C Updater
	mkdir -p dist/tmp/PSP/GAME/UPDATE/
	cp Updater/EBOOT.PBP dist/tmp/PSP/GAME/UPDATE/
	cd dist/tmp/ && zip -m -r ARK_UPDATE.zip * && cd ../../ && mv dist/tmp/ARK_UPDATE.zip dist/
	rm -r dist/tmp/
	rm -rf Updater/Resources/LIBS
	rm -rf Updater/Resources/ARK_01234
	rm -rf Updater/Resources/APPS

clean:
	rm -rf dist
	rm -rf build
	rm -rf PSVita/build
	rm -f PSVita/res/psp/*
	rm -f PSVita/res/psx/*
	rm -f PSVita/res/rif/*
	rm -rf PSVita/res/save/ARK_01234
	rm -rf $(CHOVYSIGNDIR)/output
	rm -f Resources/Language/Translations/LANG.ARK
	rm -f PSVita/loader/psp/eboot/psploader.iso
	rm -f PSVita/loader/psp/eboot/iso_files/psp_game/sysdir/*
	rm -f PSVita/loader/psp/eboot/iso_files/psp_game/ICON0.PNG
	rm -f PSVita/loader/psp/pboot/ICON0.PNG
	make -C PSP clean
	make -C Browser clean
	make -C Launcher clean
	rm -rf Launcher/shared
	rm -rf Browser/dist
	make -C PluginManager clean
	rm -rf PluginManager/dist
	make -C XMBControl clean
	make -C VSHControl clean
	make -C Updater clean
	make -C Resources/Peops clean
	make -C PSVita/loader/psp/eboot clean
	make -C PSVita/loader/psp/pboot clean
	make -C PSVita/loader/psx clean
	make -C PSVita/loader/psx/payload clean
