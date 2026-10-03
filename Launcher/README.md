# Custom Launcher

ARK-5's Custom Launcher, `ARK_01234/VBOOT.PBP`: the menu that replaces the
XMB when it isn't available. This is its source
([PSP-Arkfive/Launcher-Menus](https://github.com/PSP-Arkfive/Launcher-Menus),
commit `2457b08` of 16 August 2026, `arkMenu/` without the themes), the
version the binary FasterARK shipped until now was built from.

FasterARK builds it to replace its FTP server, the one on the Network screen.
Upstream links the server from libpspftp, which serves one client at a time
(FileZilla's second connection waits until it times out, and the server
then stops for good) and has no TLS. `ftpd/` is a new server with the same
functions:

- explicit FTPS (`AUTH TLS`, RFC 4217) with TLS 1.2, ChaCha20-Poly1305 first,
  and session resumption on data connections, which FileZilla requires;
  clients that don't ask for TLS, like Windows Explorer, still work;
- up to six connections at once, so FileZilla works with its default
  settings, and timeouts on every wait, so a client that vanishes can't
  hold the server;
- 128 KB transfer buffers (the old server received uploads 1 KB at a time),
  `REST`/`APPE` to resume transfers, `MLSD`/`MDTM` with real file dates;
- data connections only from the client's own address;
- the PSP doesn't go to sleep during transfers.

The TLS key and certificate are made the first time the server starts and
kept in `ARK_01234/FTPS_KEY.PEM` and `FTPS_CRT.PEM`; delete them for new
ones. The Network screen shows the certificate's SHA-256 fingerprint, which
FileZilla shows when it asks whether to trust the certificate.

Changes to upstream's files:

- `Makefile`: builds `ftpd/` and the Plugin Manager's entropy pool for
  mbedTLS, and links mbedTLS. libpspftp is still linked for the launcher's
  FTP client (`ftp:/` in the file browser);
- `src/net_mgr.cpp`: tells the server where ARK's folder is, and locks the
  Network screen's messages, which every connection's thread adds to.

The top-level `make launcher` builds it into `build/VBOOT.PBP`, which the
packages and the updater install. When upstream updates the launcher, this
folder should be updated to the same commit and the changes above applied
again.

## Tests

`make -C ftpd/tests check` runs the server on a PC (`tests/sys_host.c` in
place of `sys_psp.c`), built with AddressSanitizer, against an FTPS client
that behaves like FileZilla with GnuTLS (`tests/fzclient.c`), Python's
ftplib, curl and lftp: transfers, several clients at once, the connection
limit, paths, active mode, a full memory stick, and stopping with clients
connected. Needs `libmbedtls-dev`, `libgnutls28-dev`, `curl` and `lftp`.

`ftpd/tests/psp` builds the server into a bare PSP app that runs it like the
launcher does, for PPSSPP: start it, then connect `tests/fzclient` to
127.0.0.1 port 21 (PPSSPP opens the PSP's ports on the PC).
