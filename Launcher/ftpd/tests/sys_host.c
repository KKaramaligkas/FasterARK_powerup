/*
    ARK Custom Launcher FTP server (FasterARK)
    tests/sys_host.c: sys.h on a PC, for the host tests. Native paths
    ("ms0:/ISO") live under $FTPD_ROOT ("$FTPD_ROOT/ms0/ISO").
*/

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../sys.h"

#define MAX_LOCKS   16
#define MAX_DIRS    64

static void host_path(const char *native, char *out, size_t size)
{
    const char *root = getenv("FTPD_ROOT");
    const char *colon = strchr(native, ':');
    if (!root) root = ".";
    if (colon) snprintf(out, size, "%s/%.*s%s", root, (int)(colon - native), native, colon + 1);
    else snprintf(out, size, "%s/%s", root, native);
}

static int would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR;
}

static void nonblocking(int s)
{
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
}

int sys_tcp_listen(int port, int backlog, int buffers)
{
    struct sockaddr_in addr;
    int on = 1;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -errno;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (buffers > 0) sys_sock_buffers(s, buffers);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(s, backlog) < 0) {
        int r = -errno;
        close(s);
        return r;
    }
    nonblocking(s);
    return s;
}

int sys_tcp_accept(int s, unsigned char ip[4])
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int c = accept(s, (struct sockaddr *)&addr, &len);
    if (c < 0) return would_block() ? SYS_AGAIN : SYS_ERROR;
    memcpy(ip, &addr.sin_addr.s_addr, 4);
    nonblocking(c);
    return c;
}

int sys_tcp_connect(const unsigned char ip[4], int port)
{
    struct sockaddr_in addr;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return SYS_ERROR;
    nonblocking(s);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, ip, 4);
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0 && !would_block()) {
        close(s);
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
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0) return SYS_ERROR;
    return err == 0 ? 1 : SYS_ERROR;
}

int sys_sock_send(int s, const void *buf, int len)
{
    int r = send(s, buf, len, MSG_NOSIGNAL);
    if (r >= 0) return r;
    return would_block() ? SYS_AGAIN : SYS_ERROR;
}

int sys_sock_recv(int s, void *buf, int len)
{
    int r = recv(s, buf, len, 0);
    if (r >= 0) return r;
    return would_block() ? SYS_AGAIN : SYS_ERROR;
}

int sys_sock_ready(int s, int write)
{
    fd_set set;
    struct timeval now = { 0, 0 };
    FD_ZERO(&set);
    FD_SET(s, &set);
    int r = select(s + 1, write ? NULL : &set, write ? &set : NULL, NULL, &now);
    if (r < 0) return errno == EINTR ? 0 : SYS_ERROR;
    return r > 0;
}

int sys_sock_local(int s, unsigned char ip[4])
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(s, (struct sockaddr *)&addr, &len) < 0) return SYS_ERROR;
    memcpy(ip, &addr.sin_addr.s_addr, 4);
    return 0;
}

void sys_sock_buffers(int s, int size)
{
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
}

void sys_sock_close(int s)
{
    if (s >= 0) close(s);
}

static void fill_stat(const struct stat *in, sys_stat_t *st)
{
    struct tm tm;
    memset(st, 0, sizeof(*st));
    st->is_dir = S_ISDIR(in->st_mode);
    st->size = in->st_size;
    if (localtime_r(&in->st_mtime, &tm)) {
        st->year = tm.tm_year + 1900;
        st->month = tm.tm_mon + 1;
        st->day = tm.tm_mday;
        st->hour = tm.tm_hour;
        st->minute = tm.tm_min;
        st->second = tm.tm_sec;
    }
}

int sys_stat(const char *path, sys_stat_t *st)
{
    char p[1024];
    struct stat s;
    host_path(path, p, sizeof(p));
    if (stat(p, &s) < 0) return SYS_ERROR;
    fill_stat(&s, st);
    return 0;
}

int sys_open_read(const char *path)
{
    char p[1024];
    host_path(path, p, sizeof(p));
    int fd = open(p, O_RDONLY);
    if (fd >= 0) {
        struct stat s;
        if (fstat(fd, &s) == 0 && S_ISDIR(s.st_mode)) {
            close(fd);
            return SYS_ERROR;
        }
    }
    return fd < 0 ? SYS_ERROR : fd;
}

int sys_open_write(const char *path, int append, int truncate)
{
    char p[1024];
    host_path(path, p, sizeof(p));
    int fd = open(p, O_WRONLY | O_CREAT | (truncate ? O_TRUNC : 0), 0644);
    if (fd >= 0 && append && lseek(fd, 0, SEEK_END) < 0) {
        close(fd);
        return SYS_ERROR;
    }
    return fd < 0 ? SYS_ERROR : fd;
}

long long sys_seek(int fd, long long offset)
{
    return lseek(fd, offset, SEEK_SET);
}

int sys_read(int fd, void *buf, int len)
{
    return read(fd, buf, len);
}

int sys_write(int fd, const void *buf, int len)
{
    /* FTPD_DISK_LIMIT: pretend the memory stick fills up at this many bytes */
    const char *limit = getenv("FTPD_DISK_LIMIT");
    if (limit) {
        off_t at = lseek(fd, 0, SEEK_CUR);
        long long max = atoll(limit);
        if (at + len > max) {
            if (at >= max) return -1;
            len = (int)(max - at);
        }
    }
    return write(fd, buf, len);
}

void sys_close(int fd)
{
    close(fd);
}

static struct {
    DIR *dir;
    char path[1024];
} dirs[MAX_DIRS];
static pthread_mutex_t dirs_lock = PTHREAD_MUTEX_INITIALIZER;

int sys_dir_open(const char *path)
{
    char p[1024];
    host_path(path, p, sizeof(p));
    DIR *d = opendir(p);
    if (!d) return SYS_ERROR;
    pthread_mutex_lock(&dirs_lock);
    for (int i = 0; i < MAX_DIRS; i++)
        if (!dirs[i].dir) {
            dirs[i].dir = d;
            snprintf(dirs[i].path, sizeof(dirs[i].path), "%s", p);
            pthread_mutex_unlock(&dirs_lock);
            return i;
        }
    pthread_mutex_unlock(&dirs_lock);
    closedir(d);
    return SYS_ERROR;
}

int sys_dir_read(int dir, char *name, int size, sys_stat_t *st)
{
    struct dirent *e = readdir(dirs[dir].dir);
    char p[2048];
    struct stat s;
    if (!e) return 0;
    snprintf(name, size, "%s", e->d_name);
    snprintf(p, sizeof(p), "%s/%s", dirs[dir].path, e->d_name);
    if (stat(p, &s) < 0) memset(&s, 0, sizeof(s));
    fill_stat(&s, st);
    return 1;
}

void sys_dir_close(int dir)
{
    pthread_mutex_lock(&dirs_lock);
    closedir(dirs[dir].dir);
    dirs[dir].dir = NULL;
    pthread_mutex_unlock(&dirs_lock);
}

int sys_mkdir(const char *path)
{
    char p[1024];
    host_path(path, p, sizeof(p));
    return mkdir(p, 0755) < 0 ? SYS_ERROR : 0;
}

int sys_rmdir(const char *path)
{
    char p[1024];
    host_path(path, p, sizeof(p));
    return rmdir(p) < 0 ? SYS_ERROR : 0;
}

int sys_remove(const char *path)
{
    char p[1024];
    host_path(path, p, sizeof(p));
    return unlink(p) < 0 ? SYS_ERROR : 0;
}

int sys_rename(const char *from, const char *to)
{
    char a[1024], b[1024];
    host_path(from, a, sizeof(a));
    host_path(to, b, sizeof(b));
    return rename(a, b) < 0 ? SYS_ERROR : 0;
}

int sys_read_file(const char *path, unsigned char *buf, int size)
{
    int fd = sys_open_read(path);
    if (fd < 0) return SYS_ERROR;
    int n = read(fd, buf, size);
    close(fd);
    return n;
}

int sys_write_file(const char *path, const void *buf, int len)
{
    int fd = sys_open_write(path, 0, 1);
    if (fd < 0) return SYS_ERROR;
    int n = write(fd, buf, len);
    close(fd);
    return n == len ? 0 : SYS_ERROR;
}

unsigned long long sys_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void sys_sleep_ms(int ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    if (ms <= 0) t.tv_nsec = 100000;
    nanosleep(&t, NULL);
}

void sys_local_time(sys_stat_t *now)
{
    struct stat s;
    memset(&s, 0, sizeof(s));
    s.st_mtime = time(NULL);
    fill_stat(&s, now);
}

int sys_utc_offset(void)
{
    struct tm tm;
    time_t now = time(NULL);
    localtime_r(&now, &tm);
    return (int)(tm.tm_gmtoff / 60);
}

typedef struct {
    int (*fn)(void *);
    void *arg;
} start_args;

static void *thread_entry(void *p)
{
    start_args a = *(start_args *)p;
    free(p);
    a.fn(a.arg);
    return NULL;
}

int sys_thread_spawn(const char *name, int (*fn)(void *), void *arg, int stack_kb)
{
    static int next_id = 1;
    pthread_t t;
    pthread_attr_t attr;
    start_args *a = malloc(sizeof(*a));
    if (!a) return SYS_ERROR;
    a->fn = fn;
    a->arg = arg;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &attr, thread_entry, a) != 0) {
        pthread_attr_destroy(&attr);
        free(a);
        return SYS_ERROR;
    }
    pthread_attr_destroy(&attr);
    return __sync_fetch_and_add(&next_id, 1);
}

static pthread_mutex_t locks[MAX_LOCKS];
static int lock_used[MAX_LOCKS];
static pthread_mutex_t locks_lock = PTHREAD_MUTEX_INITIALIZER;

int sys_lock_create(void)
{
    pthread_mutex_lock(&locks_lock);
    for (int i = 0; i < MAX_LOCKS; i++)
        if (!lock_used[i]) {
            lock_used[i] = 1;
            pthread_mutex_init(&locks[i], NULL);
            pthread_mutex_unlock(&locks_lock);
            return i;
        }
    pthread_mutex_unlock(&locks_lock);
    return SYS_ERROR;
}

void sys_lock(int lock)
{
    if (lock >= 0) pthread_mutex_lock(&locks[lock]);
}

void sys_unlock(int lock)
{
    if (lock >= 0) pthread_mutex_unlock(&locks[lock]);
}

void sys_lock_delete(int lock)
{
    if (lock < 0) return;
    pthread_mutex_lock(&locks_lock);
    pthread_mutex_destroy(&locks[lock]);
    lock_used[lock] = 0;
    pthread_mutex_unlock(&locks_lock);
}

void sys_keep_awake(void)
{
}

void sys_add_entropy(const void *data, int len)
{
    (void)data;
    (void)len;
}
