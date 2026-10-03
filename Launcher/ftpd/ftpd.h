/*
    ARK Custom Launcher FTP server (FasterARK)
    ftpd.h: the server behind the launcher's Network screen. Same functions
    as libpspftp's server, which this replaces, plus ftpdSetDataDir().
*/

#ifndef FTPD_H
#define FTPD_H

#ifdef __cplusplus
extern "C" {
#endif

/* The drive clients see as "/": "ms0:" (memory stick) or "ef0:" (PSP Go). */
void ftpdSetDevice(char *device);
char *ftpdGetDevice();
/* Receives one line for the Network screen at a time (calls are serialized). */
void ftpdSetMsgHandler(void (*handler)(const char *));
/* Where the TLS key and certificate are kept (ARK's folder, ending in '/'). */
void ftpdSetDataDir(const char *dir);
/* Runs the server until ftpdExitHandler(); start it in its own thread. */
int ftpdLoop(unsigned int argc, void *argv);
/* Asks ftpdLoop() to finish: it closes every connection and returns. */
int ftpdExitHandler(unsigned int argc, void *argv);

#ifdef __cplusplus
}
#endif

#endif
