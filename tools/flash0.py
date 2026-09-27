#!/usr/bin/env python3
"""
Lists or replaces files inside ARK's FLASH0.ARK (the archive of ARK modules
that the installer and the updater flash).

  flash0.py list FLASH0.ARK
  flash0.py replace FLASH0.ARK OUT.ARK /kd/ark_xmbctrl.prx=path/to/file [...]

The archive is a u32 file count followed by, for each file, a u32 size, a u8
name length, the name and the data (little endian, see psp-cfw-sdk pack.py).
"""

import struct
import sys


def read_archive(path):
    data = open(path, "rb").read()
    count, = struct.unpack_from("<I", data, 0)
    pos = 4
    files = []
    for _ in range(count):
        size, = struct.unpack_from("<I", data, pos)
        name_len = data[pos + 4]
        name = data[pos + 5:pos + 5 + name_len].decode()
        start = pos + 5 + name_len
        if start + size > len(data):
            sys.exit("%s: truncated archive (%s)" % (path, name))
        files.append([name, data[start:start + size]])
        pos = start + size
    if pos != len(data):
        sys.exit("%s: %d unexpected bytes after the last file" % (path, len(data) - pos))
    return files


def write_archive(path, files):
    out = [struct.pack("<I", len(files))]
    for name, content in files:
        raw = name.encode()
        out.append(struct.pack("<IB", len(content), len(raw)) + raw + content)
    open(path, "wb").write(b"".join(out))


def main(argv):
    if len(argv) >= 3 and argv[1] == "list":
        for name, content in read_archive(argv[2]):
            print("%8d  %s" % (len(content), name))
        return 0
    if len(argv) >= 5 and argv[1] == "replace":
        files = read_archive(argv[2])
        for spec in argv[4:]:
            name, _, source = spec.partition("=")
            matches = [f for f in files if f[0] == name]
            if not matches or not source:
                sys.exit("%s: no file named %s" % (argv[2], name))
            matches[0][1] = open(source, "rb").read()
            print("replaced %s with %s" % (name, source))
        write_archive(argv[3], files)
        return 0
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
