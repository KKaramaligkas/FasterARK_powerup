/*
    ARK Custom Launcher FTP server (FasterARK)
    tests/fzclient.c: an FTPS client that behaves like FileZilla, with
    GnuTLS like FileZilla, for the host tests and the emulator test:

        fzclient HOST PORT [-plain] OP...

    OP is one of: ls DIR, put LOCAL REMOTE, get REMOTE LOCAL, mkd DIR,
    rmd DIR, dele PATH, ren FROM TO, size PATH, cwd DIR, raw "COMMAND".

    Like FileZilla it logs in after AUTH TLS, sends PBSZ 0 and PROT P, opens
    every data connection with PASV, and fails a transfer when:
    - the data connection's TLS session doesn't resume the control
      connection's ("TLS session resumption on data connection failed");
    - a download ends without TLS close_notify ("The TLS connection was
      non-properly terminated").
    Prints the certificate's SHA-256 fingerprint as "fingerprint xx:xx:...".
*/

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

/* FileZilla's priority string (libfilezilla), minimum TLS 1.2 */
#define PRIORITY "SECURE256:+SECURE128:-ARCFOUR-128:-3DES-CBC:-MD5:+SIGN-ALL:-SIGN-RSA-MD5:-VERS-SSL3.0:-VERS-TLS1.0:-VERS-TLS1.1"

static const char *host;
static int port, plain;
static int ctrl = -1;
static gnutls_session_t ctrl_tls;
static gnutls_certificate_credentials_t creds;
static char inbuf[8192];
static int inlen;

static void die(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "fzclient: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(code);
}

static int tcp_connect(const char *h, int p)
{
    struct sockaddr_in addr;
    struct timeval tv = { 30, 0 };
    int s = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p);
    if (inet_pton(AF_INET, h, &addr.sin_addr) != 1) die(2, "bad address %s", h);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) die(2, "connect %s:%d: %s", h, p, strerror(errno));
    return s;
}

static gnutls_session_t tls_client(int sock, gnutls_session_t resume_from)
{
    gnutls_session_t s;
    const char *err;
    int r;
    gnutls_init(&s, GNUTLS_CLIENT);
    if (gnutls_priority_set_direct(s, PRIORITY, &err) < 0) die(2, "priority string at %s", err);
    gnutls_credentials_set(s, GNUTLS_CRD_CERTIFICATE, creds);
    gnutls_transport_set_int(s, sock);
    gnutls_handshake_set_timeout(s, 30000);
    if (resume_from) {
        gnutls_datum_t data;
        if (gnutls_session_get_data2(resume_from, &data) < 0) die(3, "no session data to resume");
        gnutls_session_set_data(s, data.data, data.size);
        gnutls_free(data.data);
    }
    do r = gnutls_handshake(s);
    while (r < 0 && !gnutls_error_is_fatal(r));
    if (r < 0) die(3, "TLS handshake failed: %s", gnutls_strerror(r));
    return s;
}

static void print_fingerprint(gnutls_session_t s)
{
    unsigned int n = 0;
    const gnutls_datum_t *certs = gnutls_certificate_get_peers(s, &n);
    unsigned char fp[32];
    size_t size = sizeof(fp);
    if (!certs || !n || gnutls_fingerprint(GNUTLS_DIG_SHA256, &certs[0], fp, &size) < 0) die(3, "no certificate");
    printf("fingerprint ");
    for (size_t i = 0; i < size; i++) printf(i ? ":%02x" : "%02x", fp[i]);
    char *desc = gnutls_session_get_desc(s);
    printf("\nsession %s\n", desc);
    gnutls_free(desc);
}

static void ctrl_send(const char *fmt, ...)
{
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    line[n++] = '\r';
    line[n++] = '\n';
    int r = ctrl_tls ? gnutls_record_send(ctrl_tls, line, n) : send(ctrl, line, n, 0);
    if (r != n) die(2, "control send failed");
}

/* Reads one reply (all its lines); returns the code and keeps the text. */
static int ctrl_reply(char *text, int size)
{
    int code = 0, total = 0;
    text[0] = 0;
    for (;;) {
        char *nl = memchr(inbuf, '\n', inlen);
        if (nl) {
            int n = nl - inbuf + 1;
            if (total + n < size) {
                memcpy(text + total, inbuf, n);
                total += n;
                text[total] = 0;
            }
            char first[5] = { 0 };
            memcpy(first, inbuf, n < 4 ? n : 4);
            memmove(inbuf, nl + 1, inlen - n);
            inlen -= n;
            if (!code && n >= 4) code = atoi(first);
            /* the last line is "NNN text" */
            if (n >= 4 && atoi(first) == code && first[3] == ' ') return code;
            continue;
        }
        int r = ctrl_tls ? gnutls_record_recv(ctrl_tls, inbuf + inlen, sizeof(inbuf) - inlen)
                         : recv(ctrl, inbuf + inlen, sizeof(inbuf) - inlen, 0);
        if (r <= 0) die(2, "control connection closed (%s)", ctrl_tls && r < 0 ? gnutls_strerror(r) : "end");
        inlen += r;
    }
}

static int command(int expect, char *text, int size, const char *fmt, ...)
{
    char line[2048], scratch[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (!text) {
        text = scratch;
        size = sizeof(scratch);
    }
    ctrl_send("%s", line);
    int code = ctrl_reply(text, size);
    if (expect && code / 100 != expect) die(4, "%s -> %s", line, text);
    return code;
}

/* PASV, then the data connection; with TLS, the session must resume. */
static int open_pasv(void)
{
    char text[512];
    int h[4], p[2];
    command(2, text, sizeof(text), "PASV");
    char *paren = strchr(text, '(');
    if (!paren || sscanf(paren + 1, "%d,%d,%d,%d,%d,%d", &h[0], &h[1], &h[2], &h[3], &p[0], &p[1]) != 6)
        die(4, "bad PASV reply %s", text);
    /* FileZilla uses the control connection's address for unroutable replies */
    return tcp_connect(host, p[0] * 256 + p[1]);
}

static gnutls_session_t data_tls(int sock)
{
    if (plain) return NULL;
    gnutls_session_t s = tls_client(sock, ctrl_tls);
    if (!gnutls_session_is_resumed(s)) die(5, "TLS session resumption on data connection failed");
    return s;
}

/* A download: until close_notify, which FileZilla requires. */
static long long receive(const char *cmd, FILE *out)
{
    char text[1024], buf[65536];
    long long total = 0;
    int sock = open_pasv();
    command(1, text, sizeof(text), "%s", cmd);
    gnutls_session_t s = data_tls(sock);
    for (;;) {
        ssize_t r = s ? gnutls_record_recv(s, buf, sizeof(buf)) : recv(sock, buf, sizeof(buf), 0);
        if (r == 0) break;
        if (r < 0) {
            if (s && (r == GNUTLS_E_AGAIN || r == GNUTLS_E_INTERRUPTED)) continue;
            die(6, "%s: %s", cmd, s ? gnutls_strerror(r) : strerror(errno));
        }
        if (out) fwrite(buf, 1, r, out);
        total += r;
    }
    if (s) {
        gnutls_bye(s, GNUTLS_SHUT_WR);
        gnutls_deinit(s);
    }
    close(sock);
    if (ctrl_reply(text, sizeof(text)) != 226) die(4, "%s ended with %s", cmd, text);
    return total;
}

static long long send_file(const char *cmd, FILE *in)
{
    char text[1024], buf[65536];
    long long total = 0;
    size_t n;
    int sock = open_pasv();
    command(1, text, sizeof(text), "%s", cmd);
    gnutls_session_t s = data_tls(sock);
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        size_t off = 0;
        while (off < n) {
            ssize_t r = s ? gnutls_record_send(s, buf + off, n - off) : send(sock, buf + off, n - off, 0);
            if (r < 0 && s && (r == GNUTLS_E_AGAIN || r == GNUTLS_E_INTERRUPTED)) continue;
            if (r <= 0) die(6, "%s: send failed", cmd);
            off += r;
        }
        total += n;
    }
    if (s) {
        gnutls_bye(s, GNUTLS_SHUT_WR);  /* close_notify ends the upload */
        gnutls_deinit(s);
    }
    close(sock);
    if (ctrl_reply(text, sizeof(text)) != 226) die(4, "%s ended with %s", cmd, text);
    return total;
}

int main(int argc, char **argv)
{
    char text[4096];
    int i = 3;

    if (argc < 3) die(2, "usage: fzclient HOST PORT [-plain] OP...");
    host = argv[1];
    port = atoi(argv[2]);
    if (argc > 3 && !strcmp(argv[3], "-plain")) {
        plain = 1;
        i++;
    }
    gnutls_global_init();
    gnutls_certificate_allocate_credentials(&creds);

    ctrl = tcp_connect(host, port);
    if (ctrl_reply(text, sizeof(text)) != 220) die(4, "greeting: %s", text);
    if (!plain) {
        command(2, NULL, 0, "AUTH TLS");
        ctrl_tls = tls_client(ctrl, NULL);
        print_fingerprint(ctrl_tls);
    }
    command(3, NULL, 0, "USER anonymous");
    command(2, NULL, 0, "PASS anonymous@example.com");
    if (!plain) {
        command(2, NULL, 0, "PBSZ 0");
        command(2, NULL, 0, "PROT P");
    }
    command(2, NULL, 0, "SYST");
    command(2, text, sizeof(text), "FEAT");
    if (!plain && !strstr(text, "AUTH TLS")) die(4, "FEAT without AUTH TLS: %s", text);
    command(2, NULL, 0, "PWD");
    command(2, NULL, 0, "TYPE I");

    for (; i < argc; i++) {
        const char *op = argv[i];
        const char *a = i + 1 < argc ? argv[i + 1] : "";
        const char *b = i + 2 < argc ? argv[i + 2] : "";
        if (!strcmp(op, "ls")) {
            char cmd[1100];
            snprintf(cmd, sizeof(cmd), "MLSD %s", a);
            printf("listing %s\n", a);
            receive(cmd, stdout);
            i += 1;
        }
        else if (!strcmp(op, "put") || !strcmp(op, "get")) {
            int put = op[0] == 'p';
            char cmd[1100];
            FILE *f = fopen(put ? a : b, put ? "rb" : "wb");
            if (!f) die(2, "can't open %s", put ? a : b);
            snprintf(cmd, sizeof(cmd), "%s %s", put ? "STOR" : "RETR", put ? b : a);
            struct timeval t0, t1;
            gettimeofday(&t0, NULL);
            long long n = put ? send_file(cmd, f) : receive(cmd, f);
            gettimeofday(&t1, NULL);
            fclose(f);
            double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6;
            printf("%s %s %lld bytes %.2f s\n", put ? "put" : "get", put ? b : a, n, secs);
            i += 2;
        }
        else if (!strcmp(op, "mkd")) { command(2, NULL, 0, "MKD %s", a); i++; }
        else if (!strcmp(op, "rmd")) { command(2, NULL, 0, "RMD %s", a); i++; }
        else if (!strcmp(op, "dele")) { command(2, NULL, 0, "DELE %s", a); i++; }
        else if (!strcmp(op, "cwd")) { command(2, NULL, 0, "CWD %s", a); i++; }
        else if (!strcmp(op, "size")) {
            command(2, text, sizeof(text), "SIZE %s", a);
            printf("size %s", text + 4);
            i++;
        }
        else if (!strcmp(op, "ren")) {
            command(3, NULL, 0, "RNFR %s", a);
            command(2, NULL, 0, "RNTO %s", b);
            i += 2;
        }
        else if (!strcmp(op, "raw")) {
            int code = command(0, text, sizeof(text), "%s", a);
            printf("raw %d %s", code, text);
            i++;
        }
        else die(2, "unknown op %s", op);
        fflush(stdout);
    }

    command(2, NULL, 0, "QUIT");
    if (ctrl_tls) {
        gnutls_bye(ctrl_tls, GNUTLS_SHUT_WR);
        gnutls_deinit(ctrl_tls);
    }
    close(ctrl);
    gnutls_certificate_free_credentials(creds);
    gnutls_global_deinit();
    printf("ok\n");
    return 0;
}
