/*
    ARK Custom Launcher FTP server (FasterARK)
    sys_psp.c: sys.h on the PSP.
*/

#include <stdio.h>
#include <string.h>

#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <psppower.h>
#include <psprtc.h>
#include <psputility.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "sys.h"

/* the PSP's inet error numbers, and the BSD ones in case */
#define PSP_EAGAIN      11
#define PSP_EINPROGRESS 119
#define BSD_EAGAIN      35
#define BSD_EINPROGRESS 36

/* from the Plugin Manager's entropy.c, which also provides getentropy() for mbedTLS */
void entropy_add(const void *data, size_t len);

/* sceNetInetSelect() takes 256-bit descriptor sets; newlib's fd_set is
   smaller, and PPSSPP would read (and clear) past its end. */
typedef struct {
    unsigned int bits[8];
} psp_fdset;

static unsigned short net16(int v)
{
    return (unsigned short)(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
}

static int would_block(void)
{
    int e = sceNetInetGetErrno();
    return e == PSP_EAGAIN || e == PSP_EINPROGRESS || e == BSD_EAGAIN || e == BSD_EINPROGRESS;
}

static int failure(void)
{
    int e = sceNetInetGetErrno();
    return e > 0 ? -e : SYS_ERROR;
}

static void nonblocking(int s)
{
    int on = 1;
    sceNetInetSetsockopt(s, SOL_SOCKET, SO_NONBLOCK, &on, sizeof(on));
}

/* PPSSPP ignores SO_NONBLOCK and leaves accepted sockets blocking, so a
   recv() with nothing to read would freeze the whole emulator: accept,
   send and recv only go ahead when select() says they won't wait. */
static int not_ready(int s, int write)
{
    int r = sys_sock_ready(s, write);
    return r < 0 ? SYS_ERROR : r == 0 ? SYS_AGAIN : 0;
}

static void fill_addr(struct sockaddr_in *addr, const unsigned char ip[4], int port)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_len = sizeof(*addr);
    addr->sin_family = AF_INET;
    addr->sin_port = net16(port);
    if (ip) memcpy(&addr->sin_addr.s_addr, ip, 4);
}

int sys_tcp_listen(int port, int backlog, int buffers)
{
    struct sockaddr_in addr;
    int on = 1;
    int s = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return failure();
    sceNetInetSetsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    /* set before listen(), so that accepted sockets advertise the bigger window */
    if (buffers > 0) sys_sock_buffers(s, buffers);
    fill_addr(&addr, NULL, port);
    if (sceNetInetBind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0 || sceNetInetListen(s, backlog) < 0) {
        int r = failure();
        sceNetInetClose(s);
        return r;
    }
    nonblocking(s);
    return s;
}

int sys_tcp_accept(int s, unsigned char ip[4])
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int r = not_ready(s, 0);
    if (r) return r;
    memset(&addr, 0, sizeof(addr));
    int c = sceNetInetAccept(s, (struct sockaddr *)&addr, &len);
    if (c < 0) return would_block() ? SYS_AGAIN : SYS_ERROR;
    memcpy(ip, &addr.sin_addr.s_addr, 4);
    nonblocking(c);
    return c;
}

int sys_tcp_connect(const unsigned char ip[4], int port)
{
    struct sockaddr_in addr;
    int s = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return SYS_ERROR;
    nonblocking(s);
    fill_addr(&addr, ip, port);
    if (sceNetInetConnect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0 && !would_block()) {
        sceNetInetClose(s);
        return SYS_ERROR;
    }
    return s;
}

int sys_tcp_connected(int s)
{
    int err = 0;
    socklen_t len = sizeof(err);
    int r = sys_sock_ready(s, 1);
    if (r <= 0) return r;
    if (sceNetInetGetsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0) return SYS_ERROR;
    return err == 0 ? 1 : SYS_ERROR;
}

int sys_sock_send(int s, const void *buf, int len)
{
    int r = not_ready(s, 1);
    if (r) return r;
    r = sceNetInetSend(s, buf, len, 0);
    if (r >= 0) return r;
    return would_block() ? SYS_AGAIN : SYS_ERROR;
}

int sys_sock_recv(int s, void *buf, int len)
{
    int r = not_ready(s, 0);
    if (r) return r;
    r = sceNetInetRecv(s, buf, len, 0);
    if (r >= 0) return r;
    return would_block() ? SYS_AGAIN : SYS_ERROR;
}

int sys_sock_ready(int s, int write)
{
    psp_fdset set;
    struct SceNetInetTimeval now = { 0, 0 };
    if (s < 0 || s >= 256) return SYS_ERROR;
    memset(&set, 0, sizeof(set));
    set.bits[s >> 5] = 1u << (s & 31);
    int r = sceNetInetSelect(s + 1, write ? NULL : (fd_set *)&set, write ? (fd_set *)&set : NULL, NULL, &now);
    if (r < 0) return would_block() ? 0 : SYS_ERROR;
    return r > 0;
}

int sys_sock_local(int s, unsigned char ip[4])
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (sceNetInetGetsockname(s, (struct sockaddr *)&addr, &len) >= 0 && addr.sin_addr.s_addr != 0) {
        memcpy(ip, &addr.sin_addr.s_addr, 4);
        return 0;
    }
    /* the address the access point gave us */
    union SceNetApctlInfo info;
    int a, b, c, d;
    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) < 0 || sscanf(info.ip, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
        return SYS_ERROR;
    ip[0] = a;
    ip[1] = b;
    ip[2] = c;
    ip[3] = d;
    return 0;
}

void sys_sock_buffers(int s, int size)
{
    sceNetInetSetsockopt(s, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    sceNetInetSetsockopt(s, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
}

void sys_sock_close(int s)
{
    if (s >= 0) sceNetInetClose(s);
}

static void fill_stat(const SceIoStat *in, sys_stat_t *st)
{
    memset(st, 0, sizeof(*st));
    st->is_dir = FIO_S_ISDIR(in->st_mode);
    st->size = in->st_size;
    if (in->sce_st_mtime.month >= 1 && in->sce_st_mtime.month <= 12 && in->sce_st_mtime.day >= 1) {
        st->year = in->sce_st_mtime.year;
        st->month = in->sce_st_mtime.month;
        st->day = in->sce_st_mtime.day;
        st->hour = in->sce_st_mtime.hour;
        st->minute = in->sce_st_mtime.minute;
        st->second = in->sce_st_mtime.second;
    }
}

int sys_stat(const char *path, sys_stat_t *st)
{
    SceIoStat s;
    memset(&s, 0, sizeof(s));
    if (sceIoGetstat(path, &s) < 0) return SYS_ERROR;
    fill_stat(&s, st);
    return 0;
}

int sys_open_read(const char *path)
{
    int fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    return fd < 0 ? SYS_ERROR : fd;
}

int sys_open_write(const char *path, int append, int truncate)
{
    int fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | (truncate ? PSP_O_TRUNC : 0), 0777);
    if (fd < 0) return SYS_ERROR;
    if (append && sceIoLseek(fd, 0, PSP_SEEK_END) < 0) {
        sceIoClose(fd);
        return SYS_ERROR;
    }
    return fd;
}

long long sys_seek(int fd, long long offset)
{
    return sceIoLseek(fd, offset, PSP_SEEK_SET);
}

int sys_read(int fd, void *buf, int len)
{
    return sceIoRead(fd, buf, len);
}

int sys_write(int fd, const void *buf, int len)
{
    return sceIoWrite(fd, buf, len);
}

void sys_close(int fd)
{
    sceIoClose(fd);
}

int sys_dir_open(const char *path)
{
    int d = sceIoDopen(path);
    return d < 0 ? SYS_ERROR : d;
}

int sys_dir_read(int dir, char *name, int size, sys_stat_t *st)
{
    SceIoDirent e;
    memset(&e, 0, sizeof(e));       /* d_private must be NULL */
    int r = sceIoDread(dir, &e);
    if (r <= 0) return r < 0 ? SYS_ERROR : 0;
    snprintf(name, size, "%s", e.d_name);
    fill_stat(&e.d_stat, st);
    return 1;
}

void sys_dir_close(int dir)
{
    sceIoDclose(dir);
}

int sys_mkdir(const char *path)
{
    return sceIoMkdir(path, 0777) < 0 ? SYS_ERROR : 0;
}

int sys_rmdir(const char *path)
{
    return sceIoRmdir(path) < 0 ? SYS_ERROR : 0;
}

int sys_remove(const char *path)
{
    return sceIoRemove(path) < 0 ? SYS_ERROR : 0;
}

int sys_rename(const char *from, const char *to)
{
    return sceIoRename(from, to) < 0 ? SYS_ERROR : 0;
}

int sys_read_file(const char *path, unsigned char *buf, int size)
{
    int fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return SYS_ERROR;
    int n = sceIoRead(fd, buf, size);
    sceIoClose(fd);
    return n;
}

int sys_write_file(const char *path, const void *buf, int len)
{
    int fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return SYS_ERROR;
    int n = sceIoWrite(fd, buf, len);
    sceIoClose(fd);
    return n == len ? 0 : SYS_ERROR;
}

unsigned long long sys_now_ms(void)
{
    return sceKernelGetSystemTimeWide() / 1000;
}

void sys_sleep_ms(int ms)
{
    sceKernelDelayThread(ms > 0 ? ms * 1000 : 100);
}

void sys_local_time(sys_stat_t *now)
{
    ScePspDateTime t;
    memset(now, 0, sizeof(*now));
    if (sceRtcGetCurrentClockLocalTime(&t) < 0) return;
    now->year = t.year;
    now->month = t.month;
    now->day = t.day;
    now->hour = t.hour;
    now->minute = t.minute;
    now->second = t.second;
}

int sys_utc_offset(void)
{
    int tz = 0, dst = 0;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIMEZONE, &tz);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS, &dst);
    return tz + (dst ? 60 : 0);
}

typedef struct {
    int (*fn)(void *);
    void *arg;
} start_args;

static int thread_entry(SceSize args, void *argp)
{
    start_args a = *(start_args *)argp;
    a.fn(a.arg);
    sceKernelExitDeleteThread(0);
    return 0;
}

int sys_thread_spawn(const char *name, int (*fn)(void *), void *arg, int stack_kb)
{
    start_args a = { fn, arg };
    SceUID t = sceKernelCreateThread(name, thread_entry, 0x18, stack_kb * 1024, PSP_THREAD_ATTR_USBWLAN, NULL);
    if (t < 0) return t;
    int r = sceKernelStartThread(t, sizeof(a), &a);
    if (r < 0) {
        sceKernelDeleteThread(t);
        return r;
    }
    return t;
}

int sys_lock_create(void)
{
    SceUID s = sceKernelCreateSema("ftpd", 0, 1, 1, NULL);
    return s < 0 ? SYS_ERROR : s;
}

void sys_lock(int lock)
{
    if (lock >= 0) sceKernelWaitSema(lock, 1, NULL);
}

void sys_unlock(int lock)
{
    if (lock >= 0) sceKernelSignalSema(lock, 1);
}

void sys_lock_delete(int lock)
{
    if (lock >= 0) sceKernelDeleteSema(lock);
}

void sys_keep_awake(void)
{
    static unsigned long long last;
    unsigned long long now = sys_now_ms();
    if (now - last < 1000) return;
    last = now;
    scePowerTick(PSP_POWER_TICK_SUSPEND);
}

void sys_add_entropy(const void *data, int len)
{
    entropy_add(data, len);
}
