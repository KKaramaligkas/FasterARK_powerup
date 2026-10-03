/*
    ARK Custom Launcher FTP server (FasterARK)
    tls.h: TLS 1.2 for explicit FTPS (AUTH TLS, RFC 4217).

    The server has one identity: an ECDSA P-256 key and a self-signed
    certificate, made the first time and kept next to ARK's settings so that
    FTP clients see the same certificate every time and can be told to trust
    it. One configuration serves every connection, with a session cache so
    that a client's data connections resume the session of its control
    connection, which FileZilla requires.
*/

#ifndef FTPD_TLS_H
#define FTPD_TLS_H

typedef struct ftps_conn ftps_conn;

/* Loads or makes the identity kept in dir (ending in '/') and prepares the
   configuration. say() reports what happened, including the certificate's
   fingerprint. 0, or < 0 if TLS can't be offered. */
int ftps_start(const char *dir, void (*say)(const char *line));
void ftps_stop(void);
int ftps_available(void);

/* Handshake as the server on a connected socket, which stays the caller's
   to close. NULL on failure, with the reason in err. */
ftps_conn *ftps_accept(int sock, int timeout_ms, volatile int *stop, char *err, int errlen);
/* Bytes read, 0 when the client closed the connection (close_notify or the
   end of the stream), < 0 on an error, a timeout or a stop. */
int ftps_read(ftps_conn *c, void *buf, int len, int timeout_ms, volatile int *stop);
/* Writes everything: 0, or < 0 on an error, a timeout or a stop. */
int ftps_write(ftps_conn *c, const void *buf, int len, int timeout_ms, volatile int *stop);
/* Sends close_notify if notify is set (the end of a transfer or of the
   control connection), then frees the connection. */
void ftps_close(ftps_conn *c, int notify);
/* "TLS 1.2, CHACHA20-POLY1305" style description of the connection. */
void ftps_describe(ftps_conn *c, char *out, int size);

#endif
