#!/usr/bin/env python3
"""Reject moving or unchecked downloads in the official store."""
import json
import re
import sys
from urllib.parse import urlparse


def validate(store):
    for entry in store["entries"]:
        for step in entry["install"]:
            if step["type"] != "download":
                continue
            url = step["url"]
            path = urlparse(url).path
            if any(piece in path.split("/") for piece in ("latest", "nightly", "main", "master")):
                raise ValueError(f"{entry['id']}: moving download URL")
            if not (re.fullmatch(r"[0-9a-fA-F]{64}", step.get("sha256", "")) or
                    (step.get("sha256Url", "").startswith("https://") and step.get("checksumFile"))):
                raise ValueError(f"{entry['id']}: missing release checksum")
            if "sha256Url" in step and urlparse(step["sha256Url"]).path.rsplit("/", 1)[0] != path.rsplit("/", 1)[0]:
                raise ValueError(f"{entry['id']}: checksum manifest must belong to the same release")
            if entry["id"] == "ark" and f"/ARK-{entry['version']}/" not in path:
                raise ValueError("ark: version does not match the release tag")


if __name__ == "__main__":
    validate(json.load(open(sys.argv[1], encoding="utf-8")))
    print("Official store downloads are pinned and checksummed")
