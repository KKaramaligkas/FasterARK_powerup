import hashlib
import importlib.util
import io
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("ci_toolchains", Path(__file__).parents[1] / "ci_toolchains.py")
tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tools)


class LockedInputs(unittest.TestCase):
    def test_library_layouts(self):
        for prefix in ("", "arm-vita-eabi/", "vitasdk/arm-vita-eabi/"):
            with self.subTest(prefix=prefix), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                archive = root / "library.tar.xz"
                with tarfile.open(archive, "w:xz") as tar:
                    info = tarfile.TarInfo(prefix + "lib/libtest.a")
                    info.size = 3
                    tar.addfile(info, io.BytesIO(b"lib"))
                tools.install_vita_library(archive, root / "sdk")
                self.assertEqual((root / "sdk/arm-vita-eabi/lib/libtest.a").read_bytes(), b"lib")

    def test_rejects_escaping_library(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive = root / "library.tar"
            with tarfile.open(archive, "w") as tar:
                info = tarfile.TarInfo("../outside")
                info.size = 1
                tar.addfile(info, io.BytesIO(b"x"))
            with self.assertRaises(ValueError):
                tools.install_vita_library(archive, root / "sdk")
            self.assertFalse((root / "outside").exists())

    def test_bad_download_keeps_previous_file(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "sdk.tar"
            output.write_bytes(b"original")
            lock = {"downloads": {"test": {"url": "https://example.org/sdk.tar", "sha256": "0" * 64}}}

            def fake_curl(args, **kwargs):
                Path(args[args.index("--output") + 1]).write_bytes(b"wrong")

            with patch.object(tools, "read_lock", return_value=lock), patch.object(tools.subprocess, "run", fake_curl):
                with self.assertRaises(ValueError):
                    tools.download("test", output)
            self.assertEqual(output.read_bytes(), b"original")
            self.assertFalse(output.with_name("sdk.tar.part").exists())


if __name__ == "__main__":
    unittest.main()
