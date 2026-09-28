#!/usr/bin/env python3
"""
Fails the build when the store's "ark" entry doesn't carry the version being
built (Updater/version.h, written to VERSION.TXT by "make version"). The
Plugin Manager offers ARK updates by comparing the two.

Usage: check_store_version.py VERSION.TXT store.json
"""

import json
import sys


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    with open(argv[1]) as f:
        version = f.read().strip()
    with open(argv[2], encoding="utf-8") as f:
        store = json.load(f)
    ark = [e for e in store.get("entries", []) if e.get("id") == "ark"]
    if not ark:
        sys.exit("%s: no \"ark\" entry" % argv[2])
    if ark[0].get("version") != version:
        sys.exit("%s: the \"ark\" entry has version %s, but this build is %s. Update it with the release."
                 % (argv[2], ark[0].get("version"), version))
    print("store.json: ark %s matches" % version)


if __name__ == "__main__":
    main(sys.argv)
