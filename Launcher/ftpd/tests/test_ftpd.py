#!/usr/bin/env python3
"""Host tests for the launcher's FTP server: tests/server (built with
AddressSanitizer) against fzclient (FTPS the way FileZilla does it, with
GnuTLS), Python's ftplib with and without TLS, curl and lftp; several
clients at once, the connection limit, paths, active mode, a full memory
stick, and stopping with clients connected."""

import calendar
import ftplib
import hashlib
import os
import shutil
import signal
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "server")
FZCLIENT = os.path.join(HERE, "fzclient")
ARK = os.path.join("ms0", "PSP", "SAVEDATA", "ARK_01234")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def random_file(path, size):
    with open(path, "wb") as f:
        f.write(os.urandom(size))
    return path


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


class Server:
    """tests/server on a free port, serving a temporary memory stick."""

    def __init__(self, root=None, env=None):
        self.own_root = root is None
        self.root = root or tempfile.mkdtemp(prefix="ftpd-")
        os.makedirs(os.path.join(self.root, ARK), exist_ok=True)
        self.port = free_port()
        self.lines = []
        self.ready = threading.Event()
        environment = dict(os.environ, FTPD_ROOT=self.root, **(env or {}))
        self.proc = subprocess.Popen([SERVER, str(self.port)], stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, env=environment)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        if not self.ready.wait(30):
            self.proc.kill()
            raise RuntimeError("the server didn't start:\n" + "\n".join(self.lines))

    def _read(self):
        for line in self.proc.stdout:
            self.lines.append(line.rstrip("\n"))
            if line.startswith("Waiting for FTP clients"):
                self.ready.set()

    @property
    def ms0(self):
        return os.path.join(self.root, "ms0")

    def fingerprint(self):
        i = self.lines.index("TLS certificate SHA-256 fingerprint:")
        return self.lines[i + 1] + ":" + self.lines[i + 2]

    def stop(self, keep_root=False):
        self.proc.send_signal(signal.SIGTERM)
        code = self.proc.wait(30)
        self.reader.join(10)
        self.proc.stdout.close()
        problems = [l for l in self.lines if "Sanitizer" in l or "runtime error" in l]
        if self.own_root and not keep_root:
            shutil.rmtree(self.root, ignore_errors=True)
        return code, problems


def fz(server, *ops, plain=False, check=True):
    args = [FZCLIENT, "127.0.0.1", str(server.port)] + (["-plain"] if plain else []) + list(ops)
    r = subprocess.run(args, capture_output=True, text=True, timeout=120)
    if check and r.returncode != 0:
        raise AssertionError("fzclient %s failed (%d):\n%s\n%s\nserver:\n%s" % (
            " ".join(ops), r.returncode, r.stdout, r.stderr, "\n".join(server.lines[-30:])))
    return r


class ServerTest(unittest.TestCase):
    env = None

    def setUp(self):
        self.server = Server(env=self.env)
        self.work = tempfile.mkdtemp(prefix="ftpd-client-")
        self.addCleanup(shutil.rmtree, self.work, True)

    def tearDown(self):
        code, problems = self.server.stop()
        self.assertEqual(code, 0, "\n".join(self.server.lines[-40:]))
        self.assertEqual(problems, [], "\n".join(self.server.lines))

    def path(self, name):
        return os.path.join(self.work, name)

    def ftp(self, tls=False):
        f = ftplib.FTP_TLS(context=ssl._create_unverified_context()) if tls else ftplib.FTP()
        f.connect("127.0.0.1", self.server.port, timeout=30)
        if tls:
            f.auth()
        f.login()
        if tls:
            f.prot_p()
        self.addCleanup(f.close)
        return f


class FileZillaLike(ServerTest):
    def test_tls_session_resumption_and_transfers(self):
        src = random_file(self.path("game.iso"), 3 * 1024 * 1024 + 123)
        out = self.path("back.iso")
        r = fz(self.server, "mkd", "/ISO", "put", src, "/ISO/game.iso", "ls", "/ISO",
               "size", "/ISO/game.iso", "get", "/ISO/game.iso", out,
               "ren", "/ISO/game.iso", "/ISO/renamed.iso", "dele", "/ISO/renamed.iso", "rmd", "/ISO")
        self.assertEqual(digest(src), digest(out))
        self.assertIn("type=file;size=%d;" % os.path.getsize(src), r.stdout)
        self.assertIn("size %d" % os.path.getsize(src), r.stdout)
        self.assertIn("fingerprint " + self.server.fingerprint(), r.stdout)
        self.assertIn("(TLS1.2)", r.stdout)
        self.assertIn("CHACHA20-POLY1305", r.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.server.ms0, "ISO")))
        self.assertTrue(any(l.startswith("Received game.iso: 3.0 MB") for l in self.server.lines), self.server.lines)

    def test_same_certificate_after_a_restart(self):
        first = self.server.fingerprint()
        self.assertTrue(os.path.exists(os.path.join(self.server.root, ARK, "FTPS_KEY.PEM")))
        self.assertTrue(os.path.exists(os.path.join(self.server.root, ARK, "FTPS_CRT.PEM")))
        code, problems = self.server.stop(keep_root=True)
        self.assertEqual((code, problems), (0, []))
        root = self.server.root
        self.server = Server(root=root)
        self.server.own_root = True
        self.assertEqual(self.server.fingerprint(), first)
        self.assertNotIn("Making the TLS certificate (first start)", self.server.lines)

    def test_four_clients_at_once(self):
        results = {}

        def client(n):
            src = random_file(self.path("in%d.bin" % n), 1024 * 1024 + n)
            out = self.path("out%d.bin" % n)
            fz(self.server, "put", src, "/f%d.bin" % n, "get", "/f%d.bin" % n, out)
            results[n] = digest(src) == digest(out)

        threads = [threading.Thread(target=client, args=(n,)) for n in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(180)
        self.assertEqual(results, {0: True, 1: True, 2: True, 3: True})

    def test_plain_ftp_still_works(self):
        src = random_file(self.path("a.bin"), 200000)
        out = self.path("b.bin")
        fz(self.server, "-plain", "put", src, "/a.bin", "get", "/a.bin", out)
        self.assertEqual(digest(src), digest(out))


class Clients(ServerTest):
    def test_ftplib_plain(self):
        f = self.ftp()
        self.assertEqual(f.pwd(), "/")
        f.mkd("PSP/GAME/Test")
        src = random_file(self.path("x.bin"), 300000)
        with open(src, "rb") as fh:
            f.storbinary("STOR /PSP/GAME/Test/EBOOT.PBP", fh)
        f.cwd("/PSP/GAME/Test")
        self.assertEqual(f.nlst(), ["EBOOT.PBP"])
        facts = dict(f.mlsd())
        self.assertEqual(facts["EBOOT.PBP"]["type"], "file")
        self.assertEqual(int(facts["EBOOT.PBP"]["size"]), 300000)
        lines = []
        f.retrlines("LIST", lines.append)
        self.assertEqual(len(lines), 1)
        self.assertTrue(lines[0].startswith("-rw-rw-rw- 1 psp psp        300000 "), lines)
        self.assertEqual(f.size("EBOOT.PBP"), 300000)
        got = bytearray()
        f.retrbinary("RETR EBOOT.PBP", got.extend)
        self.assertEqual(hashlib.sha256(got).hexdigest(), digest(src))
        f.rename("EBOOT.PBP", "/PSP/GAME/Test/X.PBP")
        f.delete("X.PBP")
        f.cwd("..")
        f.rmd("Test")
        self.assertEqual(f.pwd(), "/PSP/GAME")
        f.quit()

    def test_ftplib_tls(self):
        f = self.ftp(tls=True)
        src = random_file(self.path("x.bin"), 700000)
        with open(src, "rb") as fh:
            f.storbinary("STOR x.bin", fh)
        got = bytearray()
        f.retrbinary("RETR x.bin", got.extend)
        self.assertEqual(hashlib.sha256(got).hexdigest(), digest(src))
        self.assertIn("x.bin", f.nlst())
        f.quit()

    def test_curl_ftps(self):
        src = random_file(self.path("c.bin"), 500000)
        out = self.path("c.out")
        url = "ftp://127.0.0.1:%d/up/c.bin" % self.server.port
        for args in (["-T", src, "--ftp-create-dirs", url], [url, "-o", out]):
            r = subprocess.run(["curl", "-sS", "--ssl-reqd", "-k", "--noproxy", "*"] + args,
                               capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(digest(src), digest(out))

    @unittest.skipUnless(shutil.which("lftp"), "lftp isn't installed")
    def test_lftp_mirror(self):
        local = self.path("tree")
        for d in ("SEPLUGINS", "PSP/GAME/App"):
            os.makedirs(os.path.join(local, d))
        random_file(os.path.join(local, "SEPLUGINS", "PLUGINS.TXT"), 100)
        random_file(os.path.join(local, "PSP", "GAME", "App", "EBOOT.PBP"), 400000)
        back = self.path("back")
        script = ("set ftp:ssl-force true; set ftp:ssl-protect-data true; set ssl:verify-certificate no; "
                  "set net:max-retries 1; open -p %d 127.0.0.1; mirror -R %s /tree; mirror /tree %s"
                  % (self.server.port, local, back))
        r = subprocess.run(["lftp", "-c", script], capture_output=True, text=True, timeout=120,
                           env=dict(os.environ, HOME=self.work))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr + "\n".join(self.server.lines[-20:]))
        self.assertEqual(digest(os.path.join(back, "PSP", "GAME", "App", "EBOOT.PBP")),
                         digest(os.path.join(local, "PSP", "GAME", "App", "EBOOT.PBP")))

    def test_resume_with_rest_and_appe(self):
        data = os.urandom(400000)
        f = self.ftp()
        f.storbinary("STOR r.bin", __import__("io").BytesIO(data[:150000]))
        f.storbinary("APPE r.bin", __import__("io").BytesIO(data[150000:]))
        with open(os.path.join(self.server.ms0, "r.bin"), "rb") as fh:
            self.assertEqual(fh.read(), data)
        got = bytearray()
        f.retrbinary("RETR r.bin", got.extend, rest=123456)
        self.assertEqual(bytes(got), data[123456:])
        f.storbinary("STOR r.bin", __import__("io").BytesIO(data[300000:]), rest=300000)
        with open(os.path.join(self.server.ms0, "r.bin"), "rb") as fh:
            self.assertEqual(fh.read(), data)

    def test_paths_stay_on_the_drive(self):
        outside = os.path.join(self.server.root, "secret.txt")
        with open(outside, "w") as fh:
            fh.write("not served")
        f = self.ftp()
        f.cwd("../../..")
        self.assertEqual(f.pwd(), "/")
        for path in ("../secret.txt", "/../secret.txt", "../../" + self.server.root + "/secret.txt"):
            with self.assertRaises(ftplib.error_perm):
                f.retrbinary("RETR " + path, lambda b: None)
        self.assertEqual(f.sendcmd("CWD /PSP/../PSP/./SAVEDATA"), "250 Directory changed.")
        self.assertEqual(f.pwd(), "/PSP/SAVEDATA")

    def test_port_must_point_at_the_client(self):
        f = self.ftp()
        for cmd in ("PORT 8,8,8,8,19,136", "EPRT |1|8.8.8.8|5000|", "PORT 127,0,0,1,0,80"):
            with self.assertRaises(ftplib.error_perm):
                f.sendcmd(cmd)

    def test_active_mode(self):
        f = self.ftp()
        f.set_pasv(False)
        src = random_file(self.path("act.bin"), 250000)
        with open(src, "rb") as fh:
            f.storbinary("STOR act.bin", fh)
        got = bytearray()
        f.retrbinary("RETR act.bin", got.extend)
        self.assertEqual(hashlib.sha256(got).hexdigest(), digest(src))

    def test_modification_times_are_utc(self):
        path = os.path.join(self.server.ms0, "t.bin")
        random_file(path, 10)
        when = calendar.timegm((2024, 5, 6, 7, 8, 9, 0, 0, 0))
        os.utime(path, (when, when))
        f = self.ftp()
        self.assertEqual(f.sendcmd("MDTM t.bin"), "213 20240506070809")
        self.assertEqual(dict(f.mlsd())["t.bin"]["modify"], "20240506070809")

    def test_bad_commands(self):
        f = self.ftp()
        with self.assertRaises(ftplib.error_perm) as e:
            f.sendcmd("FOO bar")
        self.assertTrue(str(e.exception).startswith("502"))
        with self.assertRaises(ftplib.error_perm) as e:
            f.sendcmd("X" * 3000)
        self.assertTrue(str(e.exception).startswith("500"))
        with self.assertRaises(ftplib.error_perm):
            f.sendcmd("RETR")
        with self.assertRaises(ftplib.error_perm):
            f.sendcmd("PBSZ 0")
        self.assertEqual(f.pwd(), "/")


class Limits(ServerTest):
    def test_seventh_connection_is_refused_until_one_closes(self):
        socks = []
        for _ in range(6):
            s = socket.create_connection(("127.0.0.1", self.server.port), timeout=10)
            self.assertTrue(s.recv(100).startswith(b"220 "))
            socks.append(s)
        extra = socket.create_connection(("127.0.0.1", self.server.port), timeout=10)
        self.assertTrue(extra.recv(100).startswith(b"421 "))
        extra.close()
        socks.pop().close()
        time.sleep(0.5)
        again = socket.create_connection(("127.0.0.1", self.server.port), timeout=10)
        self.assertTrue(again.recv(100).startswith(b"220 "))
        again.close()
        for s in socks:
            s.close()

    def test_stop_with_clients_connected(self):
        idle = self.ftp(tls=True)
        busy = self.ftp()
        conn = busy.transfercmd("STOR slow.bin")
        conn.sendall(b"x" * 1000)
        started = time.time()
        code, problems = self.server.stop(keep_root=True)
        self.assertLess(time.time() - started, 10)
        self.assertEqual((code, problems), (0, []))
        self.assertIn("Server stopped", self.server.lines)
        conn.close()
        shutil.rmtree(self.server.root, ignore_errors=True)
        self.server = Server()      # for tearDown


class FullMemoryStick(ServerTest):
    env = {"FTPD_DISK_LIMIT": "1000000"}

    def test_upload_reports_a_full_memory_stick(self):
        f = self.ftp()
        with self.assertRaises(ftplib.error_temp) as e:
            f.storbinary("STOR big.bin", __import__("io").BytesIO(os.urandom(2000000)))
        self.assertTrue(str(e.exception).startswith("452"), str(e.exception))
        self.assertEqual(f.pwd(), "/")


if __name__ == "__main__":
    unittest.main(verbosity=2)
