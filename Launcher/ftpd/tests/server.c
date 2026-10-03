/*
    ARK Custom Launcher FTP server (FasterARK)
    tests/server.c: runs the server on a PC for the host tests.

        FTPD_ROOT=dir ./server PORT

    serves dir/ms0 as "/" and keeps its TLS identity in
    dir/ms0/PSP/SAVEDATA/ARK_01234, like on the PSP. The Network screen's
    lines go to stdout. SIGTERM or SIGINT stop it the way the launcher does.
*/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "../ftpd.h"

void ftpdSetPort(int port);

static void show(const char *line)
{
    printf("%s\n", line);
    fflush(stdout);
}

static void stop(int sig)
{
    (void)sig;
    ftpdExitHandler(0, NULL);
}

int main(int argc, char **argv)
{
    static char device[] = "ms0:";
    if (argc < 2) {
        fprintf(stderr, "usage: FTPD_ROOT=dir %s PORT\n", argv[0]);
        return 2;
    }
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGPIPE, SIG_IGN);
    ftpdSetPort(atoi(argv[1]));
    ftpdSetDevice(device);
    ftpdSetDataDir("ms0:/PSP/SAVEDATA/ARK_01234/");
    ftpdSetMsgHandler(show);
    ftpdLoop(0, NULL);
    show("Server stopped");
    return 0;
}
