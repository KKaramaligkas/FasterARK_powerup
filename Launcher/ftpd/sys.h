/*
    ARK Custom Launcher FTP server (FasterARK)
    sys.h: what the server needs from the system. sys_psp.c implements it on
    the PSP, tests/sys_host.c on a PC for the host tests.

    Sockets are non-blocking. Waiting polls them and sleeps in short steps,
    because PPSSPP doesn't emulate blocking sockets (an accept() with nobody
    waiting fails at once, and select() with a timeout freezes the whole
    emulator), and so that a stop request is noticed within a few
    milliseconds.
*/

#ifndef FTPD_SYS_H
#define FTPD_SYS_H

#define SYS_AGAIN   (-2)    /* the operation would block: wait and retry */
#define SYS_ERROR   (-1)

/* Headless PPSSPP's clock races ahead of real time whenever every thread
   waits, so the emulator test builds with a large scale to keep timeouts
   long enough in real time. */
#ifndef FTPD_TIMEOUT_SCALE
#define FTPD_TIMEOUT_SCALE 1
#endif

/* Sockets. Addresses are IPv4, 4 bytes in network order. */
int sys_tcp_listen(int port, int backlog, int buffers);    /* socket, or < 0 */
int sys_tcp_accept(int s, unsigned char ip[4]);            /* socket, SYS_AGAIN or SYS_ERROR */
int sys_tcp_connect(const unsigned char ip[4], int port);  /* socket (connecting), or < 0 */
int sys_tcp_connected(int s);                              /* 1 connected, 0 not yet, < 0 failed */
int sys_sock_send(int s, const void *buf, int len);        /* bytes sent, SYS_AGAIN or SYS_ERROR */
int sys_sock_recv(int s, void *buf, int len);              /* bytes, 0 at the end, SYS_AGAIN or SYS_ERROR */
int sys_sock_ready(int s, int write);                      /* 1 ready, 0 not yet, < 0 error; never blocks */
int sys_sock_local(int s, unsigned char ip[4]);            /* our address on a connected socket */
void sys_sock_buffers(int s, int size);                    /* socket send and receive buffer sizes */
void sys_sock_close(int s);

/* Files, by native path ("ms0:/ISO/game.iso"). Times are local. */
typedef struct {
    int is_dir;
    long long size;
    int year, month, day, hour, minute, second;     /* year 0: unknown */
} sys_stat_t;

int sys_stat(const char *path, sys_stat_t *st);            /* 0, or < 0 if it doesn't exist */
int sys_open_read(const char *path);                       /* fd, or < 0 */
int sys_open_write(const char *path, int append, int truncate);
long long sys_seek(int fd, long long offset);              /* new offset, or < 0 */
int sys_read(int fd, void *buf, int len);
int sys_write(int fd, const void *buf, int len);
void sys_close(int fd);
int sys_dir_open(const char *path);                        /* handle, or < 0 */
int sys_dir_read(int dir, char *name, int size, sys_stat_t *st);  /* 1 entry, 0 at the end, < 0 error */
void sys_dir_close(int dir);
int sys_mkdir(const char *path);                           /* 0, or < 0 */
int sys_rmdir(const char *path);
int sys_remove(const char *path);
int sys_rename(const char *from, const char *to);
int sys_read_file(const char *path, unsigned char *buf, int size);      /* bytes, or < 0 */
int sys_write_file(const char *path, const void *buf, int len);         /* 0, or < 0 */

/* Time */
unsigned long long sys_now_ms(void);                       /* monotonic */
void sys_sleep_ms(int ms);
void sys_local_time(sys_stat_t *now);                      /* the date fields, local time */
int sys_utc_offset(void);                                  /* minutes local time is ahead of UTC */

/* Threads and locks. A spawned thread is deleted when its function returns. */
int sys_thread_spawn(const char *name, int (*fn)(void *), void *arg, int stack_kb);  /* id, or < 0 */
int sys_lock_create(void);
void sys_lock(int lock);
void sys_unlock(int lock);
void sys_lock_delete(int lock);

/* Keeps the PSP from going to sleep during transfers. */
void sys_keep_awake(void);
/* Feeds unpredictable data (connection times, addresses) to the TLS random
   number generator's entropy pool. */
void sys_add_entropy(const void *data, int len);

/* Waits until a socket can be read (or written), or timeout_ms passes, or
   *stop is set. 1 ready, 0 timed out or stopped, < 0 error. */
static inline int sys_sock_wait(int s, int write, int timeout_ms, volatile int *stop)
{
    unsigned long long until = sys_now_ms() + (unsigned)timeout_ms;
    for (;;) {
        int r = sys_sock_ready(s, write);
        if (r != 0) return r;
        if (stop && *stop) return 0;
        unsigned long long now = sys_now_ms();
        if (now >= until) return 0;
        sys_sleep_ms(until - now < 5 ? (int)(until - now) : 5);
    }
}

#endif
