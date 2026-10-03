/*
    ARK Custom Launcher FTP server (FasterARK)
    tls.c: TLS 1.2 for explicit FTPS with mbedTLS. See tls.h.

    mbedTLS isn't built with threading support here, so the random number
    generator and the session cache, which every connection's thread uses,
    are behind one lock.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cache.h>
#include <mbedtls/x509_crt.h>

#include "sys.h"
#include "tls.h"

#define KEY_FILE    "FTPS_KEY.PEM"
#define CERT_FILE   "FTPS_CRT.PEM"
#define PEM_MAX     4096
#define SUBJECT     "CN=ARK FTP server on PSP,O=FasterARK"

/* ChaCha20 is the fastest cipher on the PSP's CPU, which has no AES
   instructions; GCM for clients without it. ECDSA keys sign far faster
   than RSA ones on the PSP. */
static const int ciphersuites[] = {
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    0
};
static const mbedtls_ecp_group_id curves[] = {
    MBEDTLS_ECP_DP_CURVE25519, MBEDTLS_ECP_DP_SECP256R1, MBEDTLS_ECP_DP_NONE
};

static struct {
    int ready;
    int lock;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context key;
    mbedtls_x509_crt cert;
    mbedtls_ssl_cache_context cache;
    mbedtls_ssl_config conf;
} tls = { .lock = -1 };

struct ftps_conn {
    mbedtls_ssl_context ssl;
    int sock;
};

static int random_locked(void *drbg, unsigned char *out, size_t len)
{
    sys_lock(tls.lock);
    int r = mbedtls_ctr_drbg_random(drbg, out, len);
    sys_unlock(tls.lock);
    return r;
}

static int cache_get(void *cache, mbedtls_ssl_session *session)
{
    sys_lock(tls.lock);
    int r = mbedtls_ssl_cache_get(cache, session);
    sys_unlock(tls.lock);
    return r;
}

static int cache_set(void *cache, const mbedtls_ssl_session *session)
{
    sys_lock(tls.lock);
    int r = mbedtls_ssl_cache_set(cache, session);
    sys_unlock(tls.lock);
    return r;
}

static void report(void (*say)(const char *), const char *fmt, int code)
{
    char line[96], why[64];
    mbedtls_strerror(code, why, sizeof(why));
    snprintf(line, sizeof(line), fmt, why);
    if (say) say(line);
}

/* The identity saved by an earlier start, if it's still a matching pair. */
static int load_identity(const char *dir, unsigned char *buf)
{
    char path[320];
    int n;

    snprintf(path, sizeof(path), "%s%s", dir, KEY_FILE);
    if ((n = sys_read_file(path, buf, PEM_MAX - 1)) <= 0) return -1;
    buf[n] = 0;
    if (mbedtls_pk_parse_key(&tls.key, buf, n + 1, NULL, 0) != 0) return -1;

    snprintf(path, sizeof(path), "%s%s", dir, CERT_FILE);
    if ((n = sys_read_file(path, buf, PEM_MAX - 1)) <= 0) return -1;
    buf[n] = 0;
    if (mbedtls_x509_crt_parse(&tls.cert, buf, n + 1) != 0) return -1;

    if (mbedtls_pk_get_type(&tls.key) != MBEDTLS_PK_ECKEY) return -1;
    return mbedtls_pk_check_pair(&tls.cert.pk, &tls.key) == 0 ? 0 : -1;
}

/* A new key and a self-signed certificate for it, valid until 2049. */
static int make_identity(const char *dir, unsigned char *buf, void (*say)(const char *))
{
    mbedtls_x509write_cert crt;
    mbedtls_mpi serial;
    unsigned char sn[16];
    char path[320];
    int r;

    mbedtls_pk_free(&tls.key);
    mbedtls_pk_init(&tls.key);
    mbedtls_x509_crt_free(&tls.cert);
    mbedtls_x509_crt_init(&tls.cert);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_mpi_init(&serial);

    if (say) say("Making the TLS certificate (first start)");
    if ((r = mbedtls_pk_setup(&tls.key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) != 0 ||
        (r = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(tls.key), random_locked, &tls.drbg)) != 0 ||
        (r = random_locked(&tls.drbg, sn, sizeof(sn))) != 0)
        goto done;
    sn[0] &= 0x7F;      /* serial numbers are positive */
    sn[0] |= 0x40;
    if ((r = mbedtls_mpi_read_binary(&serial, sn, sizeof(sn))) != 0) goto done;

    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &tls.key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &tls.key);
    if ((r = mbedtls_x509write_crt_set_subject_name(&crt, SUBJECT)) != 0 ||
        (r = mbedtls_x509write_crt_set_issuer_name(&crt, SUBJECT)) != 0 ||
        (r = mbedtls_x509write_crt_set_serial(&crt, &serial)) != 0 ||
        (r = mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20491231235959")) != 0 ||
        (r = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1)) != 0 ||
        (r = mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE)) != 0 ||
        (r = mbedtls_x509write_crt_pem(&crt, buf, PEM_MAX, random_locked, &tls.drbg)) != 0 ||
        (r = mbedtls_x509_crt_parse(&tls.cert, buf, strlen((char *)buf) + 1)) != 0)
        goto done;

    /* keep it, so that clients see the same certificate next time */
    snprintf(path, sizeof(path), "%s%s", dir, CERT_FILE);
    int saved = sys_write_file(path, buf, strlen((char *)buf)) == 0;
    if (saved && mbedtls_pk_write_key_pem(&tls.key, buf, PEM_MAX) == 0) {
        snprintf(path, sizeof(path), "%s%s", dir, KEY_FILE);
        saved = sys_write_file(path, buf, strlen((char *)buf)) == 0;
    }
    if (!saved && say) say("Couldn't save the TLS certificate; it changes next time");

done:
    memset(buf, 0, PEM_MAX);
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&crt);
    if (r != 0) report(say, "TLS certificate failed: %s", r);
    return r;
}

/* The certificate's SHA-256 fingerprint, as FTP clients show it, in two
   lines of 16 bytes. */
static void say_fingerprint(void (*say)(const char *))
{
    unsigned char hash[32];
    char line[64];
    if (!say || mbedtls_sha256_ret(tls.cert.raw.p, tls.cert.raw.len, hash, 0) != 0) return;
    say("TLS certificate SHA-256 fingerprint:");
    for (int half = 0; half < 2; half++) {
        char *p = line;
        for (int i = 0; i < 16; i++)
            p += sprintf(p, i ? ":%02x" : "%02x", hash[half * 16 + i]);
        say(line);
    }
}

void ftps_stop(void)
{
    if (tls.lock < 0) return;
    mbedtls_ssl_config_free(&tls.conf);
    mbedtls_ssl_cache_free(&tls.cache);
    mbedtls_x509_crt_free(&tls.cert);
    mbedtls_pk_free(&tls.key);
    mbedtls_ctr_drbg_free(&tls.drbg);
    mbedtls_entropy_free(&tls.entropy);
    sys_lock_delete(tls.lock);
    tls.lock = -1;
    tls.ready = 0;
}

int ftps_start(const char *dir, void (*say)(const char *))
{
    static const char personal[] = "ARK FTP server";
    unsigned char *buf;
    int r;

    if (tls.ready) return 0;
    if ((tls.lock = sys_lock_create()) < 0) return -1;
    mbedtls_entropy_init(&tls.entropy);
    mbedtls_ctr_drbg_init(&tls.drbg);
    mbedtls_pk_init(&tls.key);
    mbedtls_x509_crt_init(&tls.cert);
    mbedtls_ssl_cache_init(&tls.cache);
    mbedtls_ssl_config_init(&tls.conf);

    if ((buf = malloc(PEM_MAX)) == NULL) {
        ftps_stop();
        return -1;
    }
    if ((r = mbedtls_ctr_drbg_seed(&tls.drbg, mbedtls_entropy_func, &tls.entropy,
                                   (const unsigned char *)personal, sizeof(personal) - 1)) != 0) {
        report(say, "TLS random generator failed: %s", r);
        free(buf);
        ftps_stop();
        return -1;
    }
    r = load_identity(dir, buf);
    if (r != 0) r = make_identity(dir, buf, say);
    memset(buf, 0, PEM_MAX);
    free(buf);
    if (r != 0) {
        ftps_stop();
        return -1;
    }

    /* A client's control connection can stay open for hours and its data
       connections must still resume it, so sessions live for a day. */
    mbedtls_ssl_cache_set_timeout(&tls.cache, 24 * 60 * 60);
    mbedtls_ssl_cache_set_max_entries(&tls.cache, 32);

    if ((r = mbedtls_ssl_config_defaults(&tls.conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT)) != 0 ||
        (r = mbedtls_ssl_conf_own_cert(&tls.conf, &tls.cert, &tls.key)) != 0) {
        report(say, "TLS setup failed: %s", r);
        ftps_stop();
        return -1;
    }
    mbedtls_ssl_conf_rng(&tls.conf, random_locked, &tls.drbg);
    mbedtls_ssl_conf_min_version(&tls.conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_ciphersuites(&tls.conf, ciphersuites);
    mbedtls_ssl_conf_curves(&tls.conf, curves);
    mbedtls_ssl_conf_session_cache(&tls.conf, &tls.cache, cache_get, cache_set);

    tls.ready = 1;
    say_fingerprint(say);
    return 0;
}

int ftps_available(void)
{
    return tls.ready;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int r = sys_sock_send(((ftps_conn *)ctx)->sock, buf, (int)len);
    if (r == SYS_AGAIN) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return r < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : r;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int r = sys_sock_recv(((ftps_conn *)ctx)->sock, buf, (int)len);
    if (r == SYS_AGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return r < 0 ? MBEDTLS_ERR_NET_RECV_FAILED : r;
}

/* After WANT_READ/WANT_WRITE: waits for the socket until the deadline.
   0 to retry, < 0 to give up. */
static int wait_for(ftps_conn *c, int r, unsigned long long until, volatile int *stop)
{
    unsigned long long now = sys_now_ms();
    if ((stop && *stop) || now >= until) return -1;
    return sys_sock_wait(c->sock, r == MBEDTLS_ERR_SSL_WANT_WRITE, (int)(until - now), stop) > 0 ? 0 : -1;
}

ftps_conn *ftps_accept(int sock, int timeout_ms, volatile int *stop, char *err, int errlen)
{
    unsigned long long until = sys_now_ms() + (unsigned)timeout_ms;
    ftps_conn *c;
    int r;

    if (err && errlen) err[0] = 0;
    if (!tls.ready || (c = calloc(1, sizeof(*c))) == NULL) return NULL;
    c->sock = sock;
    mbedtls_ssl_init(&c->ssl);
    if ((r = mbedtls_ssl_setup(&c->ssl, &tls.conf)) == 0) {
        mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);
        while ((r = mbedtls_ssl_handshake(&c->ssl)) != 0) {
            if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) break;
            if (wait_for(c, r, until, stop) < 0) {
                r = MBEDTLS_ERR_SSL_TIMEOUT;
                break;
            }
        }
    }
    if (r == 0) return c;
    if (err && errlen) mbedtls_strerror(r, err, errlen);
    mbedtls_ssl_free(&c->ssl);
    free(c);
    return NULL;
}

int ftps_read(ftps_conn *c, void *buf, int len, int timeout_ms, volatile int *stop)
{
    unsigned long long until = sys_now_ms() + (unsigned)timeout_ms;
    for (;;) {
        int r = mbedtls_ssl_read(&c->ssl, buf, len);
        if (r > 0) return r;
        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == MBEDTLS_ERR_SSL_CONN_EOF) return 0;
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) return -1;
        if (wait_for(c, r, until, stop) < 0) return -1;
    }
}

int ftps_write(ftps_conn *c, const void *buf, int len, int timeout_ms, volatile int *stop)
{
    const unsigned char *p = buf;
    unsigned long long until = sys_now_ms() + (unsigned)timeout_ms;
    while (len > 0) {
        /* after WANT_WRITE, mbedTLS needs the same data again to finish the record */
        int r = mbedtls_ssl_write(&c->ssl, p, len);
        if (r > 0) {
            p += r;
            len -= r;
            until = sys_now_ms() + (unsigned)timeout_ms;
            continue;
        }
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) return -1;
        if (wait_for(c, r, until, stop) < 0) return -1;
    }
    return 0;
}

void ftps_close(ftps_conn *c, int notify)
{
    if (!c) return;
    if (notify) {
        unsigned long long until = sys_now_ms() + 2000ULL * FTPD_TIMEOUT_SCALE;
        int r;
        while ((r = mbedtls_ssl_close_notify(&c->ssl)) == MBEDTLS_ERR_SSL_WANT_WRITE ||
               r == MBEDTLS_ERR_SSL_WANT_READ)
            if (wait_for(c, r, until, NULL) < 0) break;
    }
    mbedtls_ssl_free(&c->ssl);
    free(c);
}

void ftps_describe(ftps_conn *c, char *out, int size)
{
    const char *suite = mbedtls_ssl_get_ciphersuite(&c->ssl);
    const char *version = mbedtls_ssl_get_version(&c->ssl);
    /* "TLS-ECDHE-ECDSA-WITH-CHACHA20-POLY1305-SHA256": the cipher part */
    const char *with = suite ? strstr(suite, "-WITH-") : NULL;
    snprintf(out, size, "%s %s", version ? version : "TLS", with ? with + 6 : (suite ? suite : ""));
}
