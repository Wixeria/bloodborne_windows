from paths import ROOT
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import windows_mods as mods
from game_overlay import build_overlay as update_overlay
from test_game_overlay import metadata


class WindowsModTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "O'Brien & mods"
        self.game = self.root / 'game'
        self.out = self.root / 'out'
        (self.game / 'dvdroot_ps4/chr').mkdir(parents=True)
        (self.game / 'dvdroot_ps4/chr/a.dcx').write_bytes(b'original')
        (self.game / 'dvdroot_ps4/chr/b.dcx').write_bytes(b'untouched')
        (self.game / 'sce_sys').mkdir()
        (self.game / 'sce_sys/param.sfo').write_bytes(metadata())
        (self.game / 'eboot.bin').write_bytes(b'executable')

    def mod(self, name, layout='dvdroot_ps4/chr', contents=b'mod'):
        folder = self.root / name / layout
        folder.mkdir(parents=True)
        (folder / 'a.dcx').write_bytes(contents)
        return self.root / name

    def test_detect_dvd_bare_wrapper_and_selected_subfolder(self):
        for index, layout in enumerate(('dvdroot_ps4/chr', 'chr', 'app0/dvdroot_ps4/chr',
                'CUSA00207/dvdroot_ps4/chr', 'Extracted v2/chr')):
            with self.subTest(layout=layout):
                folder = self.mod(str(index), layout)
                mount = mods.build_overlay(self.game, self.out, [(str(index), folder)])
                self.assertEqual((mount / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')
        for name, selected in [('DVD', 'dvdroot_ps4'), ('Single', 'dvdroot_ps4/chr')]:
            root = self.mod(name)
            mount = mods.build_overlay(self.game, self.out, [(name, root / selected)])
            self.assertEqual((mount / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')

    def test_toggle_off_restore_and_priority_cache(self):
        a, b = self.mod('A', contents=b'A'), self.mod('B', contents=b'B')
        manifest = self.root / 'selection.json'
        def load(enabled, flags):
            manifest.write_text(json.dumps(dict(enabled=enabled, mods=[
                dict(name=n, path=str(p.resolve()), enabled=on) for (n,p),on in zip([('A', a),('B', b)],flags)])), encoding='utf-8')
            return mods.build_overlay(self.game, self.out, mods.layers_from_config(manifest))
        mounted = load(True, [True, True])
        self.assertEqual((mounted / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'B')
        self.assertEqual(load(True, [True, True]), mounted)
        self.assertEqual((load(True, [True, False]) / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'A')
        self.assertEqual(load(False, [True, True]), self.game.resolve())
        self.assertEqual(load(True, [False, False]), self.game.resolve())
        self.assertEqual((mods.build_overlay(self.game, self.out, [('B',b),('A',a)]) /
                          'dvdroot_ps4/chr/a.dcx').read_bytes(), b'A')
        self.assertEqual((self.game / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'original')
        self.assertEqual((a / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'A')
        self.assertTrue(os.path.samefile(mounted / 'eboot.bin', self.game / 'eboot.bin'))
        self.assertEqual({p.name for p in (mounted / 'dvdroot_ps4/chr').iterdir()}, {'a.dcx','b.dcx'})

    def test_full_dump_system_files_are_ignored(self):
        folder = self.mod('Full')
        (folder / 'eboot.bin').write_bytes(b'mod executable')
        (folder / 'sce_sys').mkdir()
        (folder / 'sce_sys/param.sfo').write_bytes(b'not loaded')
        mount = mods.build_overlay(self.game, self.out, [('Full', folder)])
        self.assertEqual((mount / 'eboot.bin').read_bytes(), b'executable')
        self.assertEqual((mount / 'sce_sys/param.sfo').read_bytes(), metadata())

    def test_case_spelling_new_files_and_source_edits(self):
        folder = self.mod('Upper', 'DVDROOT_PS4/Chr')
        (folder / 'DVDROOT_PS4/Chr/new.dcx').write_bytes(b'added')
        mount = mods.build_overlay(self.game, self.out, [('Upper',folder)])
        self.assertEqual({p.name for p in (mount / 'dvdroot_ps4/chr').iterdir()}, {'a.dcx','b.dcx','new.dcx'})
        self.assertEqual(len([p for p in mount.iterdir() if p.name.lower() == 'dvdroot_ps4']),1)
        (folder / 'DVDROOT_PS4/Chr/a.dcx').write_bytes(b'changed length')
        fresh = mods.build_overlay(self.game, self.out, [('Upper',folder)])
        self.assertNotEqual(fresh,mount)
        self.assertEqual((fresh / 'dvdroot_ps4/chr/a.dcx').read_bytes(),b'changed length')

    def test_other_drive_mod_copy_fallback_and_cache(self):
        folder = self.mod('Copy')
        link = os.link
        def cross_drive(source, target):
            if Path(source).is_relative_to(folder):
                raise OSError('simulated other volume')
            link(source,target)
        with patch('windows_mods.os.link', side_effect=cross_drive):
            mount = mods.build_overlay(self.game,self.out,[('Copy',folder)])
        self.assertFalse(os.path.samefile(mount/'dvdroot_ps4/chr/a.dcx',folder/'dvdroot_ps4/chr/a.dcx'))
        self.assertEqual(mods.build_overlay(self.game,self.out,[('Copy',folder)]),mount)

    def test_file_directory_conflict_and_output_nesting_fail_without_source_edits(self):
        folder = self.mod('Conflict','chr/a.dcx')
        with self.assertRaisesRegex(ValueError,'conflict'):
            mods.build_overlay(self.game,self.out,[('Conflict',folder)])
        with self.assertRaisesRegex(ValueError,'separate'):
            mods.build_overlay(self.game,folder/'out',[('Conflict',folder)])
        self.assertFalse(self.out.exists())
        self.assertEqual((self.game/'dvdroot_ps4/chr/a.dcx').read_bytes(),b'original')

    def test_official_update_then_mod_precedence(self):
        update = self.root/'update'
        (update/'sce_sys').mkdir(parents=True)
        (update/'sce_sys/param.sfo').write_bytes(metadata(version='01.09',category='gp'))
        (update/'eboot.bin').write_bytes(b'updated executable')
        (update/'dvdroot_ps4/chr').mkdir(parents=True)
        (update/'dvdroot_ps4/chr/a.dcx').write_bytes(b'official')
        official = update_overlay(self.game,update,self.root/'updates')
        folder = self.mod('Mod')
        mount = mods.build_overlay(official,self.out,[('Mod',folder)])
        self.assertEqual((mount/'eboot.bin').read_bytes(),b'updated executable')
        self.assertEqual((mount/'dvdroot_ps4/chr/a.dcx').read_bytes(),b'mod')
        self.assertEqual((mods.build_overlay(official,self.out,[])/'dvdroot_ps4/chr/a.dcx').read_bytes(),b'official')

    def test_cli_snapshot_and_disabled_missing_folder(self):
        folder = self.mod('CLI')
        config = self.root/'config.json'
        config.write_text(json.dumps(dict(enabled=True,mods=[dict(name='CLI',path=str(folder.resolve()),enabled=True),
             dict(name='Missing',path=str(self.root.resolve()/'missing'),enabled=False)])),encoding='utf-8')
        result = subprocess.run([sys.executable,str(ROOT/'scripts/windows_mods.py'),str(self.game),
             '--out',str(self.out),'--config',str(config)],capture_output=True,text=True,check=True)
        self.assertEqual((Path(result.stdout.strip())/'dvdroot_ps4/chr/a.dcx').read_bytes(),b'mod')

    def test_incomplete_cache_and_malformed_profile_fail(self):
        folder = self.mod('Cache')
        mount = mods.build_overlay(self.game,self.out,[('Cache',folder)])
        (mount/'dvdroot_ps4/chr/b.dcx').unlink()
        with self.assertRaisesRegex(ValueError,'Incomplete'):
            mods.build_overlay(self.game,self.out,[('Cache',folder)])
        self.assertTrue((self.game/'dvdroot_ps4/chr/b.dcx').is_file())
        config=self.root/'bad.json'
        config.write_text('{"enabled":"yes","mods":[]}',encoding='utf-8')
        with self.assertRaises(ValueError): mods.layers_from_config(config)


if __name__ == '__main__': unittest.main()
