/*
    ARK Custom Launcher FTP server (FasterARK)
    tests/psp/main.c: runs the server on the PSP (or PPSSPP) the way the
    launcher does, without the launcher's menus, for the emulator test:
    connects to the first network profile, starts ftpdLoop() in a thread
    with the launcher's 8 KB stack, and stops it when ms0:/ftpd_stop.txt
    appears. The Network screen's lines go to ms0:/ftpd_log.txt.

    Headless PPSSPP's clock races ahead of real time whenever every thread
    waits, so this builds with FTPD_TIMEOUT_SCALE (see sys.h).
*/

#include <stdio.h>
#include <string.h>

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <psputility.h>

#include "ftpd.h"

PSP_MODULE_INFO("ftpdtest", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_THRESHOLD_SIZE_KB(4 * 1024);

static void log_line(const char *line)
{
    FILE *f = fopen("ms0:/ftpd_log.txt", "a");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

static int exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

int main(void)
{
    static char device[] = "ms0:";
    char line[96];
    int state = 0, r;

    sceIoRemove("ms0:/ftpd_log.txt");
    sceUtilityLoadModule(PSP_MODULE_NET_COMMON);
    sceUtilityLoadModule(PSP_MODULE_NET_INET);
    if ((r = sceNetInit(256 * 1024, 42, 0, 42, 0)) < 0 || (r = sceNetInetInit()) < 0 ||
        (r = sceNetResolverInit()) < 0 || (r = sceNetApctlInit(10 * 1024, 48)) < 0 ||
        (r = sceNetApctlConnect(1)) < 0) {
        snprintf(line, sizeof(line), "network setup failed: %08X", r);
        log_line(line);
        sceKernelExitGame();
        return 0;
    }
    for (int i = 0; i < 300 && state != PSP_NET_APCTL_STATE_GOT_IP; i++) {
        sceNetApctlGetState(&state);
        sceKernelDelayThread(100 * 1000);
    }
    snprintf(line, sizeof(line), "apctl state %d", state);
    log_line(line);

    ftpdSetMsgHandler(log_line);
    ftpdSetDevice(device);
    ftpdSetDataDir("ms0:/PSP/SAVEDATA/ARK_01234/");
    SceUID t = sceKernelCreateThread("ftpd_main_thread", ftpdLoop, 0x18, 0x2000, 0, 0);
    sceKernelStartThread(t, 0, 0);

    /* "tick" lines show that this thread still runs while clients are
       connected (PPSSPP froze in a recv() that had nothing to read) */
    for (int i = 0; !exists("ms0:/ftpd_stop.txt"); i++) {
        if (i % 25 == 0 && exists("ms0:/ftpd_ticks.txt")) {
            snprintf(line, sizeof(line), "tick %u ms", (unsigned)(sceKernelGetSystemTimeWide() / 1000));
            log_line(line);
        }
        sceKernelDelayThread(200 * 1000);
    }
    log_line("stopping");
    ftpdExitHandler(0, NULL);
    sceKernelWaitThreadEnd(t, NULL);
    sceKernelDeleteThread(t);
    log_line("stopped");
    sceNetApctlDisconnect();
    sceKernelExitGame();
    return 0;
}
