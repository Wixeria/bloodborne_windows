from paths import ROOT
from pathlib import Path
import os
import struct
import tempfile
import unittest
from game_overlay import build_overlay


def metadata(title='CUSA03173', version='01.00', category='gd'):
    keys, values, entries = bytearray(), bytearray(), bytearray()
    for key, value in dict(TITLE_ID=title, APP_VER=version, CATEGORY=category).items():
        data = value.encode() + b'\0'
        entries += struct.pack('<HHIII', len(keys), 0x204, len(data), len(data), len(values))
        keys += key.encode() + b'\0'
        values += data
    return struct.pack('<4sIIII', b'\0PSF', 0x101, 20 + len(entries),
                       20 + len(entries) + len(keys), 3) + entries + keys + values


class UpdateTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.base, self.patch, self.out = [self.root / name for name in ('base', 'patch', 'out')]
        for root, version, category in ((self.base, '01.00', 'gd'), (self.patch, '01.09', 'gp')):
            (root / 'sce_sys').mkdir(parents=True)
            (root / 'sce_sys/param.sfo').write_bytes(metadata(version=version, category=category))
            (root / 'eboot.bin').write_bytes(version.encode())
            (root / 'dvdroot_ps4/map').mkdir(parents=True)
        (self.base / 'dvdroot_ps4/map/base.bin').write_bytes(b'base')
        (self.base / 'dvdroot_ps4/map/replaced.bin').write_bytes(b'old')
        (self.patch / 'dvdroot_ps4/map/replaced.bin').write_bytes(b'new')
        (self.patch / 'dvdroot_ps4/map/added.bin').write_bytes(b'added')

    def tearDown(self):
        self.temporary.cleanup()

    def mount(self):
        return build_overlay(self.base, self.patch, self.out)

    def test_update_precedence_fallback_cache_and_sources(self):
        mount = self.mount()
        self.assertEqual((mount / 'eboot.bin').read_bytes(), b'01.09')
        self.assertEqual((mount / 'dvdroot_ps4/map/base.bin').read_bytes(), b'base')
        self.assertEqual((mount / 'dvdroot_ps4/map/replaced.bin').read_bytes(), b'new')
        self.assertEqual((mount / 'dvdroot_ps4/map/added.bin').read_bytes(), b'added')
        self.assertTrue(os.path.samefile(mount / 'eboot.bin', self.patch / 'eboot.bin'))
        self.assertEqual((self.base / 'dvdroot_ps4/map/replaced.bin').read_bytes(), b'old')
        self.assertEqual(self.mount(), mount)
        (self.patch / 'eboot.bin').write_bytes(b'updated-source')
        fresh = self.mount()
        self.assertNotEqual(fresh, mount)
        self.assertEqual((fresh / 'eboot.bin').read_bytes(), b'updated-source')

    def test_title_mismatch_and_older_update_rejected(self):
        sfo = self.patch / 'sce_sys/param.sfo'
        sfo.write_bytes(metadata(title='CUSA99999', version='01.09', category='gp'))
        with self.assertRaisesRegex(ValueError, 'TITLE_ID mismatch'):
            self.mount()
        sfo.write_bytes(metadata(version='00.99', category='gp'))
        with self.assertRaisesRegex(ValueError, 'older'):
            self.mount()
        self.assertFalse(self.out.exists())

    def test_conflicting_file_and_directory_rejected(self):
        (self.patch / 'dvdroot_ps4/map/base.bin').mkdir()
        with self.assertRaisesRegex(ValueError, 'File/directory conflict'):
            self.mount()

    def test_output_inside_source_rejected(self):
        with self.assertRaisesRegex(ValueError, 'outside'):
            build_overlay(self.base, self.patch, self.base / 'out')

    def test_incomplete_cache_is_not_reused(self):
        mount = self.mount()
        (mount / 'eboot.bin').unlink()
        with self.assertRaisesRegex(ValueError, 'Incomplete cached'):
            self.mount()
        self.assertTrue((self.patch / 'eboot.bin').is_file())


if __name__ == '__main__':
    unittest.main()
