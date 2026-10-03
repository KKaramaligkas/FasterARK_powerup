/*
    ARK Custom Launcher FTP server (FasterARK)
    ftpd.c: the FTP server behind the launcher's Network screen, with
    explicit FTPS.

    It replaces the server half of libpspftp, a PSP FTP server from 2006.
    That one served one client at a time: FileZilla's second connection
    waited until it timed out, and a dropped connection stopped the server
    for good. It had no TLS and received uploads 1 KB at a time. Here:
    - up to MAX_CLIENTS clients at once, each in its own thread;
    - AUTH TLS, PBSZ and PROT (RFC 4217) with TLS 1.2 (tls.c), while clients
      that don't ask for TLS (Windows Explorer) still work without it;
    - PASV/EPSV, PORT/EPRT, LIST/NLST/MLSD/MLST, RETR/STOR/APPE with REST,
      SIZE, MDTM, DELE/RMD/MKD, RNFR/RNTO and FEAT;
    - data connections only from the client's own address, and timeouts on
      every wait, so a client that vanishes can't hold the server;
    - 128 KB transfer buffers and 64 KB socket buffers.
    Any user name and password log in, as before. Transfers are byte for
    byte in both ASCII and binary mode, as before.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ftpd.h"
#include "sys.h"
#include "tls.h"

#define MAX_CLIENTS     6
#define PATH_LEN        512             /* native path, drive included */
#define LINE_LEN        1024            /* longest command line */
#define XFER_SIZE       (128 * 1024)
#define DATA_BUFFERS    (64 * 1024)     /* socket buffers of data connections */
#define IDLE_MS         (5 * 60 * 1000 * FTPD_TIMEOUT_SCALE)   /* a control connection with no command */
#define STALL_MS        (60 * 1000 * FTPD_TIMEOUT_SCALE)       /* a transfer that doesn't move */
#define CONNECT_MS      (20 * 1000 * FTPD_TIMEOUT_SCALE)       /* data connections and TLS handshakes */
#define REPLY_MS        (30 * 1000 * FTPD_TIMEOUT_SCALE)
#define STOP_WAIT_MS    (5000 * FTPD_TIMEOUT_SCALE)
#define PASV_FIRST      55000
#define PASV_PORTS      1000
#define MESSAGE_LEN     60              /* fits a line of the Network screen */

typedef struct {
    int sock;
    ftps_conn *tls;
} channel;

typedef struct client {
    int slot;
    channel ctrl;
    unsigned char ip[4];            /* the client */
    unsigned char local[4];         /* our address, for PASV replies */
    char who[16];
    int pasv;                       /* listening socket of PASV/EPSV, or -1 */
    int port_set, port_port;        /* PORT/EPRT */
    unsigned char port_ip[4];
    int epsv_all;
    int prot_private;               /* PROT P: data connections use TLS */
    int user_given;
    int ascii;
    long long rest;                 /* REST offset for the next transfer */
    char cwd[PATH_LEN];             /* "/" or "/ISO/GAMES" */
    char rnfr[PATH_LEN];            /* native path from RNFR, "" if none */
    char in[LINE_LEN];              /* control input not consumed yet */
    int in_len;
    unsigned char *xfer;            /* transfer buffer, allocated on first use */
} client;

static struct {
    volatile int stop;
    volatile int running;
    int lock;                       /* clients[] and the message handler */
    int listen;
    int port;
    int pasv_next;
    client *clients[MAX_CLIENTS];
    void (*handler)(const char *);
    char device[8];
    char data_dir[256];
} srv = {
    .lock = -1, .listen = -1, .port = 21,
    .device = "ms0:", .data_dir = "ms0:/PSP/SAVEDATA/ARK_01234/",
};

static const char *const months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* ---- messages on the Network screen ---- */

static void say(const char *fmt, ...)
{
    char line[MESSAGE_LEN + 1];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n > MESSAGE_LEN) strcpy(line + MESSAGE_LEN - 3, "...");
    if (!srv.handler) return;
    sys_lock(srv.lock);
    srv.handler(line);
    sys_unlock(srv.lock);
}

static void say_line(const char *line)
{
    say("%s", line);
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

static void human_size(long long n, char *out, int size)
{
    if (n >= 1024LL * 1024 * 1024)
        snprintf(out, size, "%lld.%lld GB", n >> 30, ((n & ((1LL << 30) - 1)) * 10) >> 30);
    else if (n >= 1024 * 1024)
        snprintf(out, size, "%lld.%lld MB", n >> 20, ((n & ((1 << 20) - 1)) * 10) >> 20);
    else
        snprintf(out, size, "%lld KB", (n + 1023) >> 10);
}

static void say_transfer(const char *what, const char *path, long long bytes, unsigned long long start, int ok)
{
    char size[24];
    unsigned long long ms = sys_now_ms() - start;
    human_size(bytes, size, sizeof(size));
    if (!ok) say("%s %s stopped after %s", what, base_name(path), size);
    else say("%s %s: %s, %lld KB/s", what, base_name(path), size, ms ? (long long)(bytes * 1000 / ms / 1024) : 0LL);
}

/* ---- control connection ---- */

static int channel_write(channel *ch, const void *buf, int len, int timeout_ms)
{
    if (ch->tls) return ftps_write(ch->tls, buf, len, timeout_ms, &srv.stop);

    const char *p = buf;
    unsigned long long until = sys_now_ms() + (unsigned)timeout_ms;
    while (len > 0) {
        int r = sys_sock_send(ch->sock, p, len);
        if (r > 0) {
            p += r;
            len -= r;
            until = sys_now_ms() + (unsigned)timeout_ms;
            continue;
        }
        if (r != SYS_AGAIN) return -1;
        unsigned long long now = sys_now_ms();
        if (srv.stop || now >= until || sys_sock_wait(ch->sock, 1, (int)(until - now), &srv.stop) < 0) return -1;
    }
    return 0;
}

/* Bytes, 0 at the end of the stream, < 0 on an error, a timeout or a stop. */
static int channel_read(channel *ch, void *buf, int len, int timeout_ms)
{
    if (ch->tls) return ftps_read(ch->tls, buf, len, timeout_ms, &srv.stop);

    unsigned long long until = sys_now_ms() + (unsigned)timeout_ms;
    for (;;) {
        int r = sys_sock_recv(ch->sock, buf, len);
        if (r >= 0) return r;
        if (r != SYS_AGAIN) return -1;
        unsigned long long now = sys_now_ms();
        if (srv.stop || now >= until || sys_sock_wait(ch->sock, 0, (int)(until - now), &srv.stop) < 0) return -1;
    }
}

/* Sends a reply; multi-line ones carry their inner "\r\n" in fmt. */
static int reply(client *c, const char *fmt, ...)
{
    char buf[LINE_LEN + 256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if (n > (int)sizeof(buf) - 3) n = sizeof(buf) - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    return channel_write(&c->ctrl, buf, n, REPLY_MS);
}

/* The next command line without its CR LF: its length, -1 when the
   connection ends (or idles out), -2 for a line too long (dropped). */
static int read_line(client *c, char *line)
{
    unsigned long long idle_until = sys_now_ms() + IDLE_MS;
    int too_long = 0;
    for (;;) {
        char *nl = memchr(c->in, '\n', c->in_len);
        if (nl) {
            int n = nl - c->in, len = n;
            if (len > 0 && c->in[len - 1] == '\r') len--;
            if (!too_long) {
                memcpy(line, c->in, len);
                line[len] = 0;
            }
            c->in_len -= n + 1;
            memmove(c->in, nl + 1, c->in_len);
            return too_long ? -2 : len;
        }
        if (c->in_len == LINE_LEN) {
            too_long = 1;
            c->in_len = 0;
        }
        unsigned long long now = sys_now_ms();
        int r = now < idle_until ? channel_read(&c->ctrl, c->in + c->in_len, LINE_LEN - c->in_len, (int)(idle_until - now)) : -1;
        if (r <= 0) {
            if (r < 0 && !srv.stop && sys_now_ms() >= idle_until)
                reply(c, "421 No command for 5 minutes, closing the connection.");
            return -1;
        }
        c->in_len += r;
    }
}

/* Telnet commands (IAC IP, IAC DM...) that some clients send before ABOR. */
static void strip_telnet(char *line)
{
    unsigned char *r = (unsigned char *)line, *w = r;
    while (*r) {
        if (r[0] == 0xFF && r[1] >= 0xF0) {
            r += 2;
            continue;
        }
        *w++ = *r++;
    }
    *w = 0;
}

/* ---- paths ---- */

/* Resolves a path from the client against the working directory: the
   virtual path ("/" or "/ISO/game.iso") and the native one
   ("ms0:/ISO/game.iso"). ".." stops at "/". 0, or -1 if it's too long. */
static int resolve(const client *c, const char *arg, char *virt, char *native)
{
    char out[PATH_LEN];
    size_t len = 0;
    const char *parts[2] = { arg[0] == '/' ? "" : c->cwd, arg };

    out[0] = 0;
    for (int p = 0; p < 2; p++) {
        const char *s = parts[p];
        while (*s) {
            while (*s == '/') s++;
            if (!*s) break;
            const char *e = s;
            while (*e && *e != '/') e++;
            size_t n = e - s;
            if (n == 2 && s[0] == '.' && s[1] == '.') {
                while (len > 0 && out[len - 1] != '/') len--;
                if (len > 0) len--;
                out[len] = 0;
            }
            else if (!(n == 1 && s[0] == '.')) {
                if (len + 1 + n + strlen(srv.device) >= PATH_LEN) return -1;
                out[len++] = '/';
                memcpy(out + len, s, n);
                len += n;
                out[len] = 0;
            }
            s = e;
        }
    }
    if (len == 0) {
        strcpy(out, "/");
        len = 1;
    }
    size_t drive = strlen(srv.device);
    memcpy(virt, out, len + 1);
    memcpy(native, srv.device, drive);
    memcpy(native + drive, out, len + 1);
    return 0;
}

static int is_root(const char *virt)
{
    return virt[0] == '/' && virt[1] == 0;
}

/* The drive's root can't always be stat'ed: it's a directory. */
static int stat_path(const char *virt, const char *native, sys_stat_t *st)
{
    if (is_root(virt)) {
        memset(st, 0, sizeof(*st));
        st->is_dir = 1;
        return 0;
    }
    return sys_stat(native, st);
}

/* A path in a reply, quoted with doubled quotes (RFC 959). */
static void quote_path(const char *path, char *out, int size)
{
    int n = 0;
    while (*path && n < size - 2) {
        if (*path == '"') out[n++] = '"';
        out[n++] = *path++;
    }
    out[n] = 0;
}

/* Makes the missing folders of a file's path, like the old server did. */
static void make_parents(const char *native)
{
    char dir[PATH_LEN];
    strcpy(dir, native);
    char *p = strchr(dir, '/');
    while (p && (p = strchr(p + 1, '/')) != NULL) {
        sys_stat_t st;
        *p = 0;
        if (sys_stat(dir, &st) < 0) sys_mkdir(dir);
        *p = '/';
    }
}

/* ---- dates ---- */

static long long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long long z, int *y, int *m, int *d)
{
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yoe + era * 400) + (*m <= 2);
}

/* "YYYYMMDDHHMMSS" in UTC, for MDTM and MLSD; offset from sys_utc_offset(). */
static void utc_stamp(const sys_stat_t *st, int offset, char *out)
{
    int y = 1980, m = 1, d = 1;
    long long t = 0;
    if (st->year) {
        t = days_from_civil(st->year, st->month, st->day) * 86400 +
            st->hour * 3600 + st->minute * 60 + st->second - (long long)offset * 60;
    }
    else t = days_from_civil(1980, 1, 1) * 86400;
    long long days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    int secs = (int)(t - days * 86400);
    civil_from_days(days, &y, &m, &d);
    sprintf(out, "%04d%02d%02d%02d%02d%02d", y, m, d, secs / 3600, secs / 60 % 60, secs % 60);
}

/* ---- data connections ---- */

static void pasv_close(client *c)
{
    if (c->pasv >= 0) sys_sock_close(c->pasv);
    c->pasv = -1;
}

static int pasv_open(client *c)
{
    pasv_close(c);
    c->port_set = 0;
    for (int tries = 0; tries < 50; tries++) {
        sys_lock(srv.lock);
        int port = PASV_FIRST + srv.pasv_next;
        srv.pasv_next = (srv.pasv_next + 1) % PASV_PORTS;
        sys_unlock(srv.lock);
        if ((c->pasv = sys_tcp_listen(port, 1, DATA_BUFFERS)) >= 0) return port;
    }
    c->pasv = -1;
    return -1;
}

/* Opens the data connection that PASV/EPSV or PORT/EPRT prepared, with TLS
   after PROT P. 0, -1 if it failed, -2 if nothing was prepared. */
static int data_open(client *c, channel *d)
{
    unsigned long long until = sys_now_ms() + CONNECT_MS;
    d->sock = -1;
    d->tls = NULL;

    if (c->pasv >= 0) {
        while (d->sock < 0) {
            unsigned char ip[4];
            int s = sys_tcp_accept(c->pasv, ip);
            if (s >= 0) {
                /* only the client itself may connect */
                if (memcmp(ip, c->ip, 4) == 0) d->sock = s;
                else {
                    sys_sock_close(s);
                    say("Refused data connection from %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
                }
                continue;
            }
            unsigned long long now = sys_now_ms();
            if (s != SYS_AGAIN || srv.stop || now >= until) break;
            sys_sock_wait(c->pasv, 0, (int)(until - now), &srv.stop);
        }
        pasv_close(c);
    }
    else if (c->port_set) {
        int s = sys_tcp_connect(c->port_ip, c->port_port);
        while (s >= 0) {
            int r = sys_tcp_connected(s);
            unsigned long long now = sys_now_ms();
            if (r > 0) {
                d->sock = s;
                break;
            }
            if (r < 0 || srv.stop || now >= until) {
                sys_sock_close(s);
                break;
            }
            sys_sock_wait(s, 1, (int)(until - now), &srv.stop);
        }
    }
    else return -2;

    if (d->sock < 0) return -1;
    sys_sock_buffers(d->sock, DATA_BUFFERS);
    if (c->prot_private) {
        char why[64];
        unsigned long long now = sys_now_ms();
        d->tls = ftps_accept(d->sock, now < until ? (int)(until - now) : 1000, &srv.stop, why, sizeof(why));
        if (!d->tls) {
            say("Data TLS failed: %s", why);
            sys_sock_close(d->sock);
            d->sock = -1;
            return -1;
        }
    }
    return 0;
}

/* After a transfer, TLS ends with close_notify, which FileZilla requires
   for a download to count as complete. */
static void data_close(channel *d, int ok)
{
    if (d->tls) ftps_close(d->tls, ok);
    if (d->sock >= 0) sys_sock_close(d->sock);
    d->tls = NULL;
    d->sock = -1;
}

static int data_failed(client *c, int r)
{
    return reply(c, r == -2 ? "425 Use PASV or PORT first." : "425 Can't open the data connection.");
}

static int need_buffer(client *c)
{
    if (!c->xfer) c->xfer = malloc(XFER_SIZE);
    return c->xfer != NULL;
}

/* ---- listings ---- */

enum { LIST_LONG, LIST_NAMES, LIST_MACHINE };

/* One line of a listing; now is the local time (LIST), offset the time zone (MLSD). */
static int list_entry(char *out, int size, int kind, const char *name, const sys_stat_t *st,
                      const sys_stat_t *now, int offset)
{
    char when[16];
    if (kind == LIST_NAMES) return snprintf(out, size, "%s\r\n", name);
    if (kind == LIST_MACHINE) {
        utc_stamp(st, offset, when);
        if (st->is_dir) return snprintf(out, size, "type=dir;modify=%s; %s\r\n", when, name);
        return snprintf(out, size, "type=file;size=%lld;modify=%s; %s\r\n", st->size, when, name);
    }
    int m = st->month >= 1 && st->month <= 12 ? st->month - 1 : 0;
    if (!st->year) strcpy(when, "Jan 01  1980");
    else if (st->year == now->year) snprintf(when, sizeof(when), "%s %02d %02d:%02d", months[m], st->day, st->hour, st->minute);
    else snprintf(when, sizeof(when), "%s %02d  %04d", months[m], st->day, st->year);
    return snprintf(out, size, "%s 1 psp psp %13lld %s %s\r\n", st->is_dir ? "drwxrwxrwx" : "-rw-rw-rw-",
                    st->is_dir ? 0LL : st->size, when, name);
}

static int send_listing(client *c, char *arg, int kind)
{
    char virt[PATH_LEN], native[PATH_LEN];
    sys_stat_t st, now;

    /* "LIST -la [path]": ls options aren't a path */
    if (kind != LIST_MACHINE)
        while (*arg == '-') {
            while (*arg && *arg != ' ') arg++;
            while (*arg == ' ') arg++;
        }
    if (resolve(c, *arg ? arg : ".", virt, native) < 0) return reply(c, "501 Path too long.");
    if (stat_path(virt, native, &st) < 0) return reply(c, "550 No such file or directory.");
    if (kind == LIST_MACHINE && !st.is_dir) return reply(c, "501 Not a directory.");
    if (!need_buffer(c)) return reply(c, "451 Not enough memory.");
    int dir = -1;
    if (st.is_dir && (dir = sys_dir_open(native)) < 0) return reply(c, "550 Can't read the directory.");

    if (reply(c, "150 Here comes the directory listing.") < 0) {
        if (dir >= 0) sys_dir_close(dir);
        return -1;
    }
    channel d;
    int r = data_open(c, &d);
    if (r < 0) {
        if (dir >= 0) sys_dir_close(dir);
        return data_failed(c, r);
    }

    sys_local_time(&now);
    int offset = sys_utc_offset();
    int fill = 0, ok = 1;
    if (dir < 0) fill = list_entry((char *)c->xfer, XFER_SIZE, kind, base_name(virt), &st, &now, offset);
    else {
        char name[256];
        sys_stat_t entry;
        while (ok && (r = sys_dir_read(dir, name, sizeof(name), &entry)) > 0) {
            if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
            if (fill > XFER_SIZE - 512) {
                ok = channel_write(&d, c->xfer, fill, STALL_MS) == 0;
                fill = 0;
            }
            int n = list_entry((char *)c->xfer + fill, XFER_SIZE - fill, kind, name, &entry, &now, offset);
            if (n > 0 && n < XFER_SIZE - fill) fill += n;
        }
        sys_dir_close(dir);
    }
    if (ok && fill) ok = channel_write(&d, c->xfer, fill, STALL_MS) == 0;
    data_close(&d, ok);
    return reply(c, ok ? "226 Directory send OK." : "426 Connection closed; transfer aborted.");
}

/* ---- transfers ---- */

static int cmd_retr(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN];
    sys_stat_t st;
    long long rest = c->rest;

    c->rest = 0;
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (stat_path(virt, native, &st) < 0 || st.is_dir) return reply(c, "550 Not a file.");
    if (!need_buffer(c)) return reply(c, "451 Not enough memory.");
    int fd = sys_open_read(native);
    if (fd < 0) return reply(c, "550 Can't open the file.");
    if (rest > 0 && sys_seek(fd, rest) != rest) {
        sys_close(fd);
        return reply(c, "554 Can't restart at %lld.", rest);
    }
    if (reply(c, "150 Opening %s mode data connection for %s (%lld bytes).", c->ascii ? "ASCII" : "BINARY",
              base_name(virt), st.size > rest ? st.size - rest : 0LL) < 0) {
        sys_close(fd);
        return -1;
    }
    channel d;
    int r = data_open(c, &d);
    if (r < 0) {
        sys_close(fd);
        return data_failed(c, r);
    }

    unsigned long long start = sys_now_ms();
    long long total = 0;
    int failed = 0;     /* 1: the connection, 2: the memory stick */
    for (;;) {
        int n = sys_read(fd, c->xfer, XFER_SIZE);
        if (n <= 0) {
            if (n < 0) failed = 2;
            break;
        }
        if (channel_write(&d, c->xfer, n, STALL_MS) < 0) {
            failed = 1;
            break;
        }
        total += n;
        sys_keep_awake();
    }
    sys_close(fd);
    data_close(&d, !failed);
    say_transfer("Sent", virt, total, start, !failed);
    if (failed == 2) return reply(c, "451 Read error on the memory stick.");
    return reply(c, failed ? "426 Connection closed; transfer aborted." : "226 Transfer complete.");
}

static int store(client *c, char *arg, int append)
{
    char virt[PATH_LEN], native[PATH_LEN];
    sys_stat_t st;
    long long rest = c->rest;

    c->rest = 0;
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (is_root(virt) || (sys_stat(native, &st) == 0 && st.is_dir)) return reply(c, "550 That's a directory.");
    if (!need_buffer(c)) return reply(c, "451 Not enough memory.");
    make_parents(native);
    int fd = sys_open_write(native, append, !append && rest == 0);
    if (fd < 0) return reply(c, "550 Can't create the file.");
    if (!append && rest > 0 && sys_seek(fd, rest) != rest) {
        sys_close(fd);
        return reply(c, "554 Can't restart at %lld.", rest);
    }
    if (reply(c, "150 Ok to send data.") < 0) {
        sys_close(fd);
        return -1;
    }
    channel d;
    int r = data_open(c, &d);
    if (r < 0) {
        sys_close(fd);
        return data_failed(c, r);
    }

    unsigned long long start = sys_now_ms();
    long long total = 0;
    int fill = 0, failed = 0;   /* 1: the connection, 2: the memory stick */
    for (;;) {
        int n = channel_read(&d, c->xfer + fill, XFER_SIZE - fill, STALL_MS);
        if (n > 0) {
            fill += n;
            total += n;
        }
        else if (n < 0) failed = 1;
        /* what arrived before a failure is kept, so that REST can resume */
        if (fill == XFER_SIZE || (n <= 0 && fill > 0)) {
            if (sys_write(fd, c->xfer, fill) != fill) failed = 2;
            fill = 0;
            sys_keep_awake();
        }
        if (n <= 0 || failed) break;
    }
    sys_close(fd);
    data_close(&d, !failed);
    say_transfer("Received", virt, total, start, !failed);
    if (failed == 2) return reply(c, "452 Write error: is the memory stick full?");
    return reply(c, failed ? "426 Connection closed; transfer aborted." : "226 Transfer complete.");
}

static int cmd_stor(client *c, char *arg) { return store(c, arg, 0); }
static int cmd_appe(client *c, char *arg) { return store(c, arg, 1); }
static int cmd_list(client *c, char *arg) { return send_listing(c, arg, LIST_LONG); }
static int cmd_nlst(client *c, char *arg) { return send_listing(c, arg, LIST_NAMES); }
static int cmd_mlsd(client *c, char *arg) { return send_listing(c, arg, LIST_MACHINE); }

/* ---- commands ---- */

static int cmd_auth(client *c, char *arg)
{
    char why[64], desc[64];
    if (c->ctrl.tls) return reply(c, "503 TLS is already on.");
    if (strcasecmp(arg, "TLS") && strcasecmp(arg, "TLS-C") && strcasecmp(arg, "SSL") && strcasecmp(arg, "TLS-P"))
        return reply(c, "504 AUTH %s isn't supported; use AUTH TLS.", arg);
    if (!ftps_available()) return reply(c, "431 TLS isn't available.");
    if (reply(c, "234 AUTH %s OK.", arg) < 0) return -1;
    c->in_len = 0;
    c->ctrl.tls = ftps_accept(c->ctrl.sock, CONNECT_MS, &srv.stop, why, sizeof(why));
    if (!c->ctrl.tls) {
        say("TLS failed: %s", why);
        return -1;
    }
    /* RFC 4217: log in again; "SSL" and "TLS-P" also protect data */
    c->user_given = 0;
    c->prot_private = !strcasecmp(arg, "SSL") || !strcasecmp(arg, "TLS-P");
    ftps_describe(c->ctrl.tls, desc, sizeof(desc));
    say("%s: %s", c->who, desc);
    return 0;
}

static int cmd_pbsz(client *c, char *arg)
{
    if (!c->ctrl.tls) return reply(c, "503 PBSZ needs AUTH TLS first.");
    return reply(c, "200 PBSZ=0");
}

static int cmd_prot(client *c, char *arg)
{
    if (!c->ctrl.tls) return reply(c, "503 PROT needs AUTH TLS first.");
    if (!strcasecmp(arg, "P")) {
        c->prot_private = 1;
        return reply(c, "200 PROT now Private.");
    }
    if (!strcasecmp(arg, "C")) {
        c->prot_private = 0;
        return reply(c, "200 PROT now Clear.");
    }
    return reply(c, "536 Only PROT C and P are supported.");
}

static int cmd_user(client *c, char *arg)
{
    c->user_given = 1;
    return reply(c, "331 Any password will do.");
}

static int cmd_pass(client *c, char *arg)
{
    if (!c->user_given) return reply(c, "503 Send USER first.");
    return reply(c, "230 Logged in.");
}

static int cmd_syst(client *c, char *arg) { return reply(c, "215 UNIX Type: L8"); }
static int cmd_noop(client *c, char *arg) { return reply(c, "200 NOOP OK."); }
static int cmd_allo(client *c, char *arg) { return reply(c, "202 ALLO isn't needed."); }
static int cmd_abor(client *c, char *arg) { return reply(c, "225 No transfer to abort."); }
static int cmd_site(client *c, char *arg) { return reply(c, "502 SITE commands aren't supported."); }

static int cmd_quit(client *c, char *arg)
{
    reply(c, "221 Goodbye.");
    return -1;
}

static int cmd_feat(client *c, char *arg)
{
    return reply(c, "211-Features:\r\n%s EPSV\r\n MDTM\r\n MLST type*;size*;modify*;\r\n"
                 " REST STREAM\r\n SIZE\r\n TVFS\r\n211 End",
                 ftps_available() ? " AUTH TLS\r\n PBSZ\r\n PROT\r\n" : "");
}

static int cmd_help(client *c, char *arg)
{
    return reply(c, "214-Commands:\r\n"
                 " ABOR ALLO APPE AUTH CDUP CWD  DELE EPRT EPSV FEAT HELP LIST MDTM MKD\r\n"
                 " MLSD MLST MODE NLST NOOP OPTS PASS PASV PBSZ PORT PROT PWD  QUIT REST\r\n"
                 " RETR RMD  RNFR RNTO SITE SIZE STAT STOR STRU SYST TYPE USER\r\n214 OK.");
}

static int cmd_stat(client *c, char *arg)
{
    if (*arg) return reply(c, "502 STAT of a path isn't supported; use LIST.");
    return reply(c, "211-ARK FTP server\r\n Connected from %s\r\n Control connection: %s\r\n"
                 " Data connections: %s\r\n211 End.", c->who, c->ctrl.tls ? "TLS" : "plain",
                 c->prot_private ? "TLS" : "plain");
}

static int cmd_opts(client *c, char *arg)
{
    if (!strncasecmp(arg, "MLST", 4)) return reply(c, "200 MLST OPTS type;size;modify;");
    return reply(c, "501 Option not understood.");
}

static int cmd_type(client *c, char *arg)
{
    char t = arg[0] & ~0x20;
    if (t == 'A' && (!arg[1] || !strcasecmp(arg + 1, " N"))) {
        c->ascii = 1;
        return reply(c, "200 Switching to ASCII mode.");
    }
    if (t == 'I' || (t == 'L' && !strcmp(arg + 1, " 8"))) {
        c->ascii = 0;
        return reply(c, "200 Switching to Binary mode.");
    }
    return reply(c, "504 TYPE %s isn't supported.", arg);
}

static int cmd_mode(client *c, char *arg)
{
    return reply(c, (arg[0] & ~0x20) == 'S' && !arg[1] ? "200 Mode set to S." : "504 Only MODE S is supported.");
}

static int cmd_stru(client *c, char *arg)
{
    return reply(c, (arg[0] & ~0x20) == 'F' && !arg[1] ? "200 Structure set to F." : "504 Only STRU F is supported.");
}

static int cmd_pwd(client *c, char *arg)
{
    char quoted[PATH_LEN * 2];
    quote_path(c->cwd, quoted, sizeof(quoted));
    return reply(c, "257 \"%s\" is the current directory.", quoted);
}

static int cmd_cwd(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN];
    sys_stat_t st;
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (stat_path(virt, native, &st) < 0 || !st.is_dir) return reply(c, "550 No such directory.");
    strcpy(c->cwd, virt);
    return reply(c, "250 Directory changed.");
}

static int cmd_cdup(client *c, char *arg)
{
    char up[] = "..";
    return cmd_cwd(c, up);
}

static int cmd_mkd(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN], quoted[PATH_LEN * 2];
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (!is_root(virt)) make_parents(native);       /* like the old server */
    if (is_root(virt) || sys_mkdir(native) < 0) return reply(c, "550 Can't create the directory.");
    quote_path(virt, quoted, sizeof(quoted));
    return reply(c, "257 \"%s\" created.", quoted);
}

static int cmd_rmd(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN];
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (is_root(virt) || sys_rmdir(native) < 0) return reply(c, "550 Can't remove the directory (is it empty?).");
    return reply(c, "250 Directory removed.");
}

static int cmd_dele(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN];
    sys_stat_t st;
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (stat_path(virt, native, &st) < 0) return reply(c, "550 No such file.");
    if (st.is_dir) return reply(c, "550 That's a directory; use RMD.");
    if (sys_remove(native) < 0) return reply(c, "550 Can't delete the file.");
    return reply(c, "250 Deleted.");
}

static int cmd_rnfr(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN];
    sys_stat_t st;
    c->rnfr[0] = 0;
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (is_root(virt) || sys_stat(native, &st) < 0) return reply(c, "550 No such file or directory.");
    strcpy(c->rnfr, native);
    return reply(c, "350 Ready for RNTO.");
}

static int cmd_rnto(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN];
    if (!c->rnfr[0]) return reply(c, "503 Send RNFR first.");
    int ok = resolve(c, arg, virt, native) == 0 && !is_root(virt) && sys_rename(c->rnfr, native) == 0;
    c->rnfr[0] = 0;
    return reply(c, ok ? "250 Renamed." : "550 Can't rename.");
}

static int cmd_size(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN];
    sys_stat_t st;
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (stat_path(virt, native, &st) < 0 || st.is_dir) return reply(c, "550 Not a file.");
    return reply(c, "213 %lld", st.size);
}

static int cmd_mdtm(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN], when[16];
    sys_stat_t st;
    if (resolve(c, arg, virt, native) < 0) return reply(c, "501 Path too long.");
    if (stat_path(virt, native, &st) < 0) return reply(c, "550 No such file or directory.");
    utc_stamp(&st, sys_utc_offset(), when);
    return reply(c, "213 %s", when);
}

static int cmd_mlst(client *c, char *arg)
{
    char virt[PATH_LEN], native[PATH_LEN], line[PATH_LEN + 96];
    sys_stat_t st, now;
    if (resolve(c, *arg ? arg : ".", virt, native) < 0) return reply(c, "501 Path too long.");
    if (stat_path(virt, native, &st) < 0) return reply(c, "550 No such file or directory.");
    sys_local_time(&now);
    int n = list_entry(line, sizeof(line), LIST_MACHINE, virt, &st, &now, sys_utc_offset());
    if (n >= 2 && n < (int)sizeof(line)) line[n - 2] = 0;      /* reply() ends the line */
    return reply(c, "250-Listing %s\r\n %s\r\n250 End.", virt, line);
}

static int cmd_rest(client *c, char *arg)
{
    char *end;
    long long n = strtoll(arg, &end, 10);
    if (*end || n < 0) return reply(c, "501 REST needs a byte offset.");
    c->rest = n;
    return reply(c, "350 Restarting at %lld. Send RETR or STOR.", n);
}

static int cmd_pasv(client *c, char *arg)
{
    if (c->epsv_all) return reply(c, "520 EPSV ALL was sent; use EPSV.");
    int port = pasv_open(c);
    if (port < 0) return reply(c, "425 Can't open a passive connection.");
    return reply(c, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).", c->local[0], c->local[1], c->local[2],
                 c->local[3], port >> 8, port & 255);
}

static int cmd_epsv(client *c, char *arg)
{
    if (!strcasecmp(arg, "ALL")) {
        c->epsv_all = 1;
        return reply(c, "200 EPSV ALL OK.");
    }
    if (*arg && strcmp(arg, "1")) return reply(c, "522 Only IPv4 is supported, use (1)");
    int port = pasv_open(c);
    if (port < 0) return reply(c, "425 Can't open a passive connection.");
    return reply(c, "229 Entering Extended Passive Mode (|||%d|)", port);
}

/* Active mode: we connect to the client, but only to the client itself, so
   that the server can't be used to reach other machines (FTP bounce). */
static int set_port(client *c, const unsigned char ip[4], int port)
{
    if (c->epsv_all) return reply(c, "520 EPSV ALL was sent; use EPSV.");
    if (memcmp(ip, c->ip, 4) != 0 || port < 1024 || port > 65535)
        return reply(c, "501 PORT must be the client's own address and a port above 1023.");
    pasv_close(c);
    memcpy(c->port_ip, ip, 4);
    c->port_port = port;
    c->port_set = 1;
    return reply(c, "200 PORT command successful.");
}

static int cmd_port(client *c, char *arg)
{
    int v[6];
    char extra;
    if (sscanf(arg, "%d,%d,%d,%d,%d,%d%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &extra) != 6)
        return reply(c, "501 Syntax: PORT h1,h2,h3,h4,p1,p2");
    for (int i = 0; i < 6; i++)
        if (v[i] < 0 || v[i] > 255) return reply(c, "501 Syntax: PORT h1,h2,h3,h4,p1,p2");
    unsigned char ip[4] = { v[0], v[1], v[2], v[3] };
    return set_port(c, ip, v[4] * 256 + v[5]);
}

static int cmd_eprt(client *c, char *arg)
{
    /* EPRT |1|132.235.1.2|6275| */
    char delim = arg[0];
    int proto, a, b, cc, d, port, n = 0;
    char fmt[48];
    if (!delim) return reply(c, "501 Syntax: EPRT |1|address|port|");
    snprintf(fmt, sizeof(fmt), "%c%%d%c%%d.%%d.%%d.%%d%c%%d%c%%n", delim, delim, delim, delim);
    if (sscanf(arg, fmt, &proto, &a, &b, &cc, &d, &port, &n) != 6 || n == 0 || arg[n])
        return reply(c, "501 Syntax: EPRT |1|address|port|");
    if (proto != 1) return reply(c, "522 Only IPv4 is supported, use (1)");
    if (a < 0 || a > 255 || b < 0 || b > 255 || cc < 0 || cc > 255 || d < 0 || d > 255)
        return reply(c, "501 Bad address.");
    unsigned char ip[4] = { a, b, cc, d };
    return set_port(c, ip, port);
}

static const struct {
    const char *name;
    int (*run)(client *c, char *arg);
    int needs_arg;
} commands[] = {
    { "ABOR", cmd_abor, 0 }, { "ALLO", cmd_allo, 0 }, { "APPE", cmd_appe, 1 }, { "AUTH", cmd_auth, 1 },
    { "CDUP", cmd_cdup, 0 }, { "CWD", cmd_cwd, 1 }, { "DELE", cmd_dele, 1 }, { "EPRT", cmd_eprt, 1 },
    { "EPSV", cmd_epsv, 0 }, { "FEAT", cmd_feat, 0 }, { "HELP", cmd_help, 0 }, { "LIST", cmd_list, 0 },
    { "MDTM", cmd_mdtm, 1 }, { "MKD", cmd_mkd, 1 }, { "MLSD", cmd_mlsd, 0 }, { "MLST", cmd_mlst, 0 },
    { "MODE", cmd_mode, 1 }, { "NLST", cmd_nlst, 0 }, { "NOOP", cmd_noop, 0 }, { "OPTS", cmd_opts, 1 },
    { "PASS", cmd_pass, 0 }, { "PASV", cmd_pasv, 0 }, { "PBSZ", cmd_pbsz, 1 }, { "PORT", cmd_port, 1 },
    { "PROT", cmd_prot, 1 }, { "PWD", cmd_pwd, 0 }, { "QUIT", cmd_quit, 0 }, { "REST", cmd_rest, 1 },
    { "RETR", cmd_retr, 1 }, { "RMD", cmd_rmd, 1 }, { "RNFR", cmd_rnfr, 1 }, { "RNTO", cmd_rnto, 1 },
    { "SITE", cmd_site, 0 }, { "SIZE", cmd_size, 1 }, { "STAT", cmd_stat, 0 }, { "STOR", cmd_stor, 1 },
    { "STRU", cmd_stru, 1 }, { "SYST", cmd_syst, 0 }, { "TYPE", cmd_type, 1 }, { "USER", cmd_user, 0 },
    { "XCUP", cmd_cdup, 0 }, { "XCWD", cmd_cwd, 1 }, { "XMKD", cmd_mkd, 1 }, { "XPWD", cmd_pwd, 0 },
    { "XRMD", cmd_rmd, 1 },
};

/* 0 to go on, < 0 to close the connection. */
static int dispatch(client *c, char *line)
{
    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    int name_len = arg - line;
    if (*arg) *arg++ = 0;
    /* arguments are file names: keep inner and trailing spaces, but not the separator */

    if (!name_len) return 0;
    say("> %s%s%s from %s", line, *arg ? " " : "", strcasecmp(line, "PASS") ? arg : "***", c->who);
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcasecmp(line, commands[i].name)) continue;
        if (commands[i].needs_arg && !*arg) return reply(c, "501 %s needs an argument.", commands[i].name);
        return commands[i].run(c, arg);
    }
    return reply(c, "502 %s isn't implemented.", line);
}

/* ---- clients and the server ---- */

static int client_main(void *arg)
{
    client *c = arg;
    char line[LINE_LEN + 1];

    if (reply(c, "220 ARK FTP server ready%s.", ftps_available() ? " (TLS available)" : "") == 0) {
        for (;;) {
            int n = read_line(c, line);
            if (n == -1) break;
            if (n == -2) {
                if (reply(c, "500 Command too long.") < 0) break;
                continue;
            }
            strip_telnet(line);
            if (dispatch(c, line) < 0) break;
        }
    }

    pasv_close(c);
    if (c->ctrl.tls) ftps_close(c->ctrl.tls, 1);
    sys_sock_close(c->ctrl.sock);
    say("%s disconnected", c->who);
    free(c->xfer);
    sys_lock(srv.lock);
    srv.clients[c->slot] = NULL;
    sys_unlock(srv.lock);
    free(c);
    return 0;
}

static void start_client(int sock, const unsigned char ip[4])
{
    static const char busy[] = "421 Too many connections; try again later.\r\n";
    unsigned long long now = sys_now_ms();
    client *c = calloc(1, sizeof(*c));
    char who[16];
    int slot = -1;

    sys_add_entropy(&now, sizeof(now));
    sys_add_entropy(ip, 4);
    snprintf(who, sizeof(who), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    if (c) {
        sys_lock(srv.lock);
        for (int i = 0; i < MAX_CLIENTS && slot < 0; i++)
            if (!srv.clients[i]) srv.clients[slot = i] = c;
        sys_unlock(srv.lock);
    }
    if (slot < 0) {
        sys_sock_send(sock, busy, sizeof(busy) - 1);
        sys_sock_close(sock);
        free(c);
        say("Refused %s: too many connections", who);
        return;
    }

    c->slot = slot;
    c->ctrl.sock = sock;
    c->pasv = -1;
    memcpy(c->ip, ip, 4);
    strcpy(c->who, who);
    strcpy(c->cwd, "/");
    if (sys_sock_local(sock, c->local) < 0) memset(c->local, 0, 4);
    say("Connection from %s", who);
    if (sys_thread_spawn("ftpd_client", client_main, c, 64) < 0) {
        say("No thread for %s", who);
        sys_lock(srv.lock);
        srv.clients[slot] = NULL;
        sys_unlock(srv.lock);
        sys_sock_close(sock);
        free(c);
    }
}

static int clients_left(void)
{
    int n = 0;
    sys_lock(srv.lock);
    for (int i = 0; i < MAX_CLIENTS; i++) n += srv.clients[i] != NULL;
    sys_unlock(srv.lock);
    return n;
}

static int server_main(void *unused)
{
    srv.listen = sys_tcp_listen(srv.port, 8, 0);
    if (srv.listen < 0) {
        say("Can't use port %d (error %d)", srv.port, -srv.listen);
        srv.running = 0;
        return 0;
    }
    int tls = ftps_start(srv.data_dir, say_line) == 0;
    say(tls ? "Waiting for FTP clients (TLS available)" : "Waiting for FTP clients (no TLS)");

    while (!srv.stop) {
        unsigned char ip[4];
        int s = sys_tcp_accept(srv.listen, ip);
        if (s >= 0) start_client(s, ip);
        else if (s == SYS_AGAIN) sys_sock_wait(srv.listen, 0, 250, &srv.stop);
        else sys_sleep_ms(100);     /* e.g. a client that left before we got to it */
    }
    sys_sock_close(srv.listen);
    srv.listen = -1;

    /* the clients notice the stop within a few milliseconds */
    unsigned long long until = sys_now_ms() + STOP_WAIT_MS;
    while (clients_left() && sys_now_ms() < until) sys_sleep_ms(20);
    if (clients_left()) say("A connection didn't close in time");
    else ftps_stop();       /* otherwise kept for the stragglers, and reused */
    srv.running = 0;
    return 0;
}

void ftpdSetDevice(char *device)
{
    if (device && (!strncmp(device, "ms0:", 4) || !strncmp(device, "ef0:", 4))) {
        memcpy(srv.device, device, 4);
        srv.device[4] = 0;
    }
}

char *ftpdGetDevice()
{
    return srv.device;
}

void ftpdSetMsgHandler(void (*handler)(const char *))
{
    srv.handler = handler;
}

void ftpdSetDataDir(const char *dir)
{
    if (!dir || !dir[0] || strlen(dir) + 1 >= sizeof(srv.data_dir)) return;
    strcpy(srv.data_dir, dir);
    if (srv.data_dir[strlen(srv.data_dir) - 1] != '/') strcat(srv.data_dir, "/");
}

/* For the host tests, which can't use port 21. */
void ftpdSetPort(int port)
{
    srv.port = port;
}

int ftpdLoop(unsigned int argc, void *argv)
{
    /* the launcher starts this with a small stack: the server runs in its own thread */
    if (srv.lock < 0 && (srv.lock = sys_lock_create()) < 0) return 0;
    srv.stop = 0;
    srv.running = 1;
    if (sys_thread_spawn("ftpd_server", server_main, NULL, 64) < 0) {
        srv.running = 0;
        say("Couldn't start the FTP server");
        return 0;
    }
    while (srv.running) sys_sleep_ms(50);
    return 0;
}

int ftpdExitHandler(unsigned int argc, void *argv)
{
    srv.stop = 1;
    return 0;
}
