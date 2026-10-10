"""End-to-end loader checks using synthetic BBPROBE x86-64 images, no game dump.

python tests/test_loader.py --probe out/windows/bb-probe.exe
"""
import argparse
import base64
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import unittest

PROBE = None
SALT = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")


def nid(name, scope="#p#J"):
    digest = hashlib.sha1(name.encode("ascii") + SALT).digest()[:8][::-1]
    return base64.b64encode(digest).decode().rstrip("=").replace("/", "-") + scope


def q(*values):
    return struct.pack("<" + "Q" * len(values), *values)


class Image:
    def __init__(self, linked=False):
        self.data = bytearray(8192)
        self.code = bytearray(b"\x48\x83\xec\x08")  # SysV call alignment
        self.imports = []
        self.relocs = []
        self.segments = [(0, 4096, 5), (4096, 4096, 6)]
        self.linked = linked
        self.main_tls = (0x1800, 4, 16, 16)
        self.data[0x1800:0x1804] = b"MAIN"
        self.data[0x1820:0x1824] = b"MOD!"
        self.module = (0x200, 0x100, 0x200, 0x1820, 16, 4, 2)
        self.data[0x200:0x203] = b"\x31\xc0\xc3"  # module initializer returning zero

    def slot(self, name, kind=1):
        index = len(self.imports)
        self.imports.append(name)
        offset = 0x1000 + index * 8
        self.relocs.append((offset, kind, index, 0))
        return offset

    def rip(self, instruction, target):
        self.code += instruction + struct.pack("<i", target - len(self.code) - len(instruction) - 4)

    def call(self, name):
        self.rip(b"\xff\x15", self.slot(name))

    def exit(self, status=0):
        self.code += b"\xbf" + struct.pack("<I", status)
        self.call(nid("_exit"))
        self.code += b"\x0f\x0b"

    def check(self, instruction):
        self.code += instruction + b"\x74\x02\x0f\x0b"  # require ZF, else UD2

    def serialize(self, copy_code=True):
        if copy_code:
            self.data[:len(self.code)] = self.code
        header = (b"BBPROBE5" if self.linked else b"BBPROBE2") + q(len(self.data), 0, len(self.segments), len(self.relocs), len(self.imports), 1)
        if self.linked:
            header += q(0x1840, *self.main_tls, 1, *self.module, 0)
        header += b"".join(q(*s) for s in self.segments)
        header += b"".join(name.encode("ascii").ljust(128, b"\0") for name in self.imports)
        return header + b"".join(q(*r) for r in self.relocs) + self.data


@unittest.skipUnless(PROBE, "Supply --probe to test an actual built loader")
class Loader(unittest.TestCase):
    def run_image(self, image, expected=0, args=(), patch=None, raw=None, unicode=False):
        with tempfile.TemporaryDirectory(prefix="bb-loader-\u015f\u00f6\u00e7-" if unicode else "bb-loader-") as directory:
            path = Path(directory) / "boot.bin"
            path.write_bytes(image.serialize() if raw is None else raw)
            options = list(args)
            if patch is not None:
                patches = Path(directory) / "patches.bin"
                patches.write_bytes(patch)
                options += ["--patches", str(patches)]
            result = subprocess.run([str(PROBE), str(path), "--cpu-only", *options], capture_output=True, text=True, timeout=12)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def test_entry_and_exit(self):
        image = Image(); image.exit(7)
        output = self.run_image(image, 7)
        self.assertIn("Entering original x86-64 code", output)
        self.assertIn("guest requested _exit(7)", output)

    def test_unicode_boot_path(self):
        image = Image(); image.exit()
        self.run_image(image, unicode=True)

    def test_main_tcb(self):
        image = Image()
        image.code += bytes.fromhex("65 48 8b 04 25 00 00 00 00")
        image.check(bytes.fromhex("48 3b 00"))  # self pointer at TCB + 0
        image.exit()
        output = self.run_image(image)
        if PROBE.suffix == ".exe":
            self.assertIn("1 guest GS loads", output)

    def test_entry_arguments_are_low(self):
        image = Image()
        image.check(bytes.fromhex("48 83 3f 01"))  # argc
        image.code += bytes.fromhex("48 8b 47 08 48 c1 e8 28")
        image.check(bytes.fromhex("48 85 c0"))  # argv0 fits 40 bits
        image.exit(); self.run_image(image)

    def test_relative_relocation(self):
        image = Image()
        image.relocs.append((0x1100, 0, 0x1200, 0)); image.data[0x1200:0x1204] = b"TEST"
        image.rip(bytes.fromhex("48 8b 05"), 0x1100)
        image.check(bytes.fromhex("81 38 54 45 53 54"))
        image.exit(); self.run_image(image)

    def test_linked_initializers_and_static_tls(self):
        image = Image(linked=True)
        image.code += bytes.fromhex("65 48 8b 04 25 00 00 00 00")
        image.check(bytes.fromhex("81 78 f0 4d 41 49 4e"))  # main TLS at TCB - 16
        image.rip(bytes.fromhex("48 8d 3d"), 0x1860)
        image.data[0x1860:0x1870] = q(2, 0)
        image.call("vNe1w4diLCs#p#J")
        image.check(bytes.fromhex("81 38 4d 4f 44 21"))
        image.exit(); output = self.run_image(image)
        self.assertIn("initializer returned 0", output)

    def test_guest_thread_from_loaded_code(self):
        image = Image()
        image.rip(bytes.fromhex("48 8d 3d"), 0x1900)  # handle output
        image.code += bytes.fromhex("31 f6")  # default attributes
        image.rip(bytes.fromhex("48 8d 15"), 0x300)  # thread entry
        image.code += bytes.fromhex("b9 34 12 00 00 45 31 c0")  # argument and name
        image.call("6UgtwV+0zb4#p#J"); image.check(bytes.fromhex("85 c0"))
        image.rip(bytes.fromhex("48 8b 3d"), 0x1900)
        image.rip(bytes.fromhex("48 8d 35"), 0x1910)
        image.call("onNY9Byn-W8#p#J"); image.check(bytes.fromhex("85 c0"))
        image.rip(bytes.fromhex("48 8b 05"), 0x1910)
        image.check(bytes.fromhex("48 3d 34 12 00 00")); image.exit()
        # Raw, non-PE guest thread code verifies patched per-thread TCB then returns arg.
        image.data[0x300:0x300+22] = bytes.fromhex("65 48 8b 04 25 00 00 00 00 48 3b 00 74 02 0f 0b 48 89 f8 c3 90 90")
        output = self.run_image(image); self.assertIn("created=1", output)

    def test_unknown_import_stops(self):
        image = Image(); image.call("unknown-api#p#J")
        self.assertIn("first unsupported PS4 import", self.run_image(image, 20))

    def test_strict_imports(self):
        image = Image(); image.exit()
        self.assertIn("first unsupported PS4 import", self.run_image(image, 20, ["--strict-imports"]))

    def test_data_trap(self):
        image = Image(); image.rip(bytes.fromhex("48 8b 05"), image.slot("unknown-data#p#J", 2)); image.code += bytes.fromhex("48 8b 00")
        output = self.run_image(image, 139)
        if PROBE.suffix == ".exe": self.assertIn("Unsupported data import", output)

    def test_write_protection(self):
        image = Image(); image.rip(bytes.fromhex("48 8d 05"), 0); image.code += bytes.fromhex("c6 00 90")
        self.assertIn("fault", self.run_image(image, 139))

    def test_illegal_instruction(self):
        image = Image(); image.code += b"\x0f\x0b"; self.run_image(image, 132)

    def test_watchdog(self):
        image = Image(); image.code += b"\xeb\xfe"
        output = self.run_image(image, 124 if PROBE.suffix == ".exe" else 142, ["--timeout", "1"])
        self.assertIn("watchdog timeout", output)

    @unittest.skipUnless(os.name == "nt", "Windows diagnostic stop file")
    def test_stop_file(self):
        image = Image(); image.code += b"\xeb\xfe"
        with tempfile.TemporaryDirectory(prefix="bb-stop-") as directory:
            path = Path(directory) / "boot.bin"
            marker = Path(directory) / "dur-\u015f\u00f6\u00e7.trigger"
            path.write_bytes(image.serialize())
            env = dict(os.environ, BB_STOP_FILE=str(marker))
            process = subprocess.Popen([str(PROBE), str(path), "--cpu-only", "--timeout", "0"],
                                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                time.sleep(.3)
                self.assertIsNone(process.poll())
                marker.write_text("stop", encoding="ascii")
                output, _ = process.communicate(timeout=5)
                self.assertEqual(process.returncode, 0, output)
                self.assertIn(b"Profile: stop requested", output)
                self.assertNotIn(b"watchdog timeout", output)
            finally:
                if process.poll() is None:
                    process.kill(); process.communicate(timeout=5)

    def test_invalid_inputs(self):
        image = Image(); image.exit()
        for raw in (b"", b"BADMAGIC", image.serialize()[:-1], image.serialize()+b"x"):
            with self.subTest(length=len(raw)): self.assertIn("ERROR:", self.run_image(image, 1, raw=raw))
        image.relocs.append((0x1100, 0, 0xffffffffffffffff, 0)); self.assertIn("bad relocation", self.run_image(image, 1))

    def test_invalid_segments(self):
        for segments in ([(0, 4096, 5), (0, 4096, 6)], [(0, 0, 5)], [(0, 4096, 6)]):
            image = Image(); image.exit(); image.segments = segments
            with self.subTest(segments=segments): self.run_image(image, 1)

    def test_patch_and_pointer_rebase(self):
        image = Image(); image.relocs.append((0x1100, 0, 0x1200, 0))
        image.data[0x1200:0x1204] = b"OLD!"; image.data[0x1210:0x1214] = b"NEW!"
        image.rip(bytes.fromhex("48 8b 05"), 0x1100); image.check(bytes.fromhex("81 38 4e 45 57 21")); image.exit()
        base = 0x400000
        patch = b"BBPATCH2" + q(base, 1, 0x1100, 8, base+0x1210)
        self.assertIn("1 pointers rebased", self.run_image(image, patch=patch))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    options, remaining = parser.parse_known_args()
    PROBE = options.probe.resolve()
    # unittest discovers the class before CLI parsing, so enable it here.
    Loader.__unittest_skip__ = False
    unittest.main(argv=[__file__, *remaining])
