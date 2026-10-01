import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).parents[1] / f"{name}.py")
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


release = module("release_store")
checks = module("check_store_downloads")


class ReleaseStore(unittest.TestCase):
    def fixture(self):
        return {"entries": [{"id": name, "version": "old", "install": [
            {"type": "download", "url": "https://example.org/latest", "sha256": "old"}
        ]} for name in ("ark", "pluginmanager")]}

    def test_seed_and_published_store_have_same_versions_and_assets(self):
        seed = release.prepare(self.fixture(), "5.2.0", "1.1.0")
        checks.validate(seed)
        with tempfile.TemporaryDirectory() as folder:
            for asset in ("ARK_UPDATE.zip", "PluginManager.zip"):
                (Path(folder) / asset).write_bytes(asset.encode())
            published = release.prepare(seed, "5.2.0", "1.1.0", folder)
        checks.validate(published)
        for baked, live in zip(seed["entries"], published["entries"]):
            self.assertEqual(baked["version"], live["version"])
            self.assertEqual(baked["install"][0]["url"], live["install"][0]["url"])
            self.assertEqual(live["install"][0]["sha256"], hashlib.sha256(baked["install"][0]["checksumFile"].encode()).hexdigest())
            self.assertNotIn("sha256Url", live["install"][0])

    def test_moving_download_is_rejected(self):
        with self.assertRaises(ValueError):
            checks.validate(self.fixture())

    def test_wrong_release_manifest_is_rejected(self):
        seed = release.prepare(self.fixture(), "5.2.0", "1.1.0")
        seed["entries"][0]["install"][0]["sha256Url"] = "https://example.org/other/SHA256SUMS"
        with self.assertRaises(ValueError):
            checks.validate(seed)


if __name__ == "__main__":
    unittest.main()
