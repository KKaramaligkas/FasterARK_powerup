#!/usr/bin/env python3
"""Prepare the bundled store and resolve release checksums without a hash cycle."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "https://github.com/KKaramaligkas/FasterARK_powerup"


def versions():
    header = (ROOT / "Updater/version.h").read_text()
    numbers = [re.search(r"^#define ARK_" + part + r"_VERSION\s+(\d+)\s*$", header, re.M).group(1)
               for part in ("MAJOR", "MINOR", "MICRO")]
    suffix = re.search(r'^#define ARK_VERSION_SUFFIX\s+"([a-zA-Z0-9_-]*)"', header, re.M)
    ark = ".".join(numbers) + (suffix.group(1) if suffix else "")
    pm = re.search(r'^#define PM_VERSION\s+"([a-zA-Z0-9._-]+)"',
                   (ROOT / "PluginManager/src/version.h").read_text(), re.M).group(1)
    return ark, pm


def prepare(store, ark_version, pm_version, artifacts=None):
    store = json.loads(json.dumps(store))
    tag = "ARK-" + ark_version
    expected = {"ark": (ark_version, "ARK_UPDATE.zip"), "pluginmanager": (pm_version, "PluginManager.zip")}
    for entry in store["entries"]:
        if entry["id"] not in expected:
            continue
        version, asset = expected[entry["id"]]
        entry["version"] = version
        downloads = [step for step in entry["install"] if step["type"] == "download"]
        if len(downloads) != 1:
            raise ValueError(f"Expected one download for {entry['id']}")
        step = downloads[0]
        step["url"] = f"{REPOSITORY}/releases/download/{tag}/{asset}"
        step.pop("sha256", None)
        step.pop("sha256Url", None)
        step.pop("checksumFile", None)
        if artifacts is None:
            # This file is inside the archive being hashed. A separate, fixed
            # release manifest avoids embedding an archive's own hash in it.
            step["sha256Url"] = f"{REPOSITORY}/releases/download/{tag}/SHA256SUMS"
            step["checksumFile"] = asset
        else:
            with (Path(artifacts) / asset).open("rb") as stream:
                step["sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
    return store


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", action="store_true", help="Prepare the bundled snapshot for the current version headers")
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.seed == bool(args.artifacts):
        parser.error("Choose --seed or --artifacts")
    seed = ROOT / "PluginManager/store/store.json"
    output = seed if args.seed else args.output
    if output is None:
        parser.error("--artifacts requires --output")
    store = prepare(json.loads(seed.read_text()), *versions(), artifacts=args.artifacts)
    output.write_text(json.dumps(store, indent=2, ensure_ascii=False) + "\n")


if __name__ == "__main__":
    main()
