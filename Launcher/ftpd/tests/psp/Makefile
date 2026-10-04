# The FTP server in a bare PSP app, for the emulator test (see main.c).
TARGET = ftpdtest
OBJS = main.o ftpd.o tls.o sys_psp.o entropy.o
INCDIR = ../..
CFLAGS = -O2 -G0 -Wall -Wextra -Wno-unused-parameter -std=gnu99 -DFTPD_TIMEOUT_SCALE=2000
ASFLAGS = $(CFLAGS)
BUILD_PRX = 1
PSP_FW_VERSION = 660
LIBS = -lmbedtls -lmbedx509 -lmbedcrypto -lpspnet -lpspnet_apctl -lpspwlan -lpsppower -lpsprtc
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = FTP server test
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

%.o: ../../%.c
	$(CC) $(CFLAGS) $(addprefix -I,$(INCDIR)) -I$(PSPSDK)/include -c $< -o $@
entropy.o: ../../../../PluginManager/app/src/entropy.c
	$(CC) $(CFLAGS) -I$(PSPSDK)/include -c $< -o $@
