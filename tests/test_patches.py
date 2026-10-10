from paths import ROOT
import struct
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
from pathlib import Path

import patches

from patches import (EBOOT_BASE, OUTPUT_SIZE, RESOLUTION_TEMPLATE, SCENE_HEIGHT,
                     SCENE_WIDTH, UI_HEIGHT, UI_WIDTH, compile_patches,
                     render_size, resolution_writes, scaled_sizes, effect_patches,
                     validate_patch_requirements, external_patches, external_selection,
                     compile_external)
from patches import launch_plan

XML = ROOT / 'patches/Bloodborne.xml'
SEGMENTS = [(0, 0x6000000)]


def immediate(writes, address):
    # The XML splits a 9-byte MOV imm32 + NOP into 4+4+1 byte writes.
    data = bytearray(9)
    for offset, value in writes:
        if address <= offset < address + 9:
            data[offset-address:offset-address+len(value)] = value
    assert data[5:] == b'\x0f\x1f\x40\x00'
    return data[0], struct.unpack_from('<I', data, 1)[0]


class NativeUiTests(unittest.TestCase):
    def test_presets_keep_ui_native(self):
        for preset, expected in [(1, (1280, 720)), (2, (1130, 636)),
                                 (3, (960, 540)), (4, (640, 360))]:
            with self.subTest(preset=preset):
                size = render_size({'upscaler': 'fsr3', 'preset': str(preset)})
                self.assertEqual(size, expected)
                writes = resolution_writes(XML, size, '01.09', SEGMENTS)
                self.assertEqual(immediate(writes, SCENE_WIDTH), (0xB8, size[0]))
                self.assertEqual(immediate(writes, SCENE_HEIGHT), (0xB8, size[1]))
                self.assertEqual(immediate(writes, UI_WIDTH), (0xB8, OUTPUT_SIZE[0]))
                self.assertEqual(immediate(writes, UI_HEIGHT), (0xB9, OUTPUT_SIZE[1]))

    def test_explicit_scene_resolution_keeps_native_ui(self):
        writes = resolution_writes(XML, (800, 450), '01.09', SEGMENTS)
        self.assertEqual(immediate(writes, SCENE_WIDTH)[1], 800)
        self.assertEqual(immediate(writes, UI_WIDTH)[1], 1920)
        self.assertEqual(immediate(writes, UI_HEIGHT)[1], 1080)

    def test_coordinate_and_aspect_fixes_are_preserved(self):
        original = compile_patches(XML, [RESOLUTION_TEMPLATE], '01.09', SEGMENTS)
        modified = resolution_writes(XML, (960, 540), '01.09', SEGMENTS)
        self.assertEqual(len(original), len(modified))
        changed = {SCENE_WIDTH, SCENE_HEIGHT, UI_WIDTH, UI_HEIGHT}
        self.assertEqual([w for w in original if w[0] not in changed],
                         [w for w in modified if w[0] not in changed])

    def test_unexpected_or_missing_ui_instruction_is_rejected(self):
        for remove in (False, True):
            tree = ET.parse(XML)
            for meta in tree.getroot().iter('Metadata'):
                if meta.get('Name') == RESOLUTION_TEMPLATE and meta.get('AppVer') == '01.09':
                    patch_list = meta.find('PatchList')
                    for line in list(patch_list):
                        if int(line.get('Address'), 0) == UI_WIDTH + EBOOT_BASE:
                            if remove:
                                patch_list.remove(line)
                            else:
                                line.set('Value', '0x000500B9')
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'patch.xml'
                tree.write(path)
                with self.assertRaises(ValueError):
                    resolution_writes(path, (960, 540), '01.09', SEGMENTS)

    def test_native_and_disabled_upscaler_need_no_resolution_patch(self):
        self.assertIsNone(render_size({'preset': '0'}))
        self.assertIsNone(render_size({'upscaler': 'off', 'preset': '3'}))

    def test_output_other_than_1080p_scales_the_scene(self):
        self.assertIsNone(scaled_sizes({'output_res': '1920x1080', 'preset': '2'}))
        # Steam Deck: below 1080p the scene is still the preset's fraction of the output.
        for preset, expected in [(0, (1280, 720)), (2, (752, 424)), (4, (426, 240))]:
            with self.subTest(preset=preset):
                self.assertEqual(scaled_sizes({'output_res': '1280x720', 'preset': str(preset)}),
                                 (expected, (1280, 720)))
        self.assertEqual(scaled_sizes({'output_res': '3840x2160', 'preset': '3'}),
                         ((1916, 1078), (3840, 2160)))
        # TAA is native-only and uses the live host targets.
        self.assertIsNone(scaled_sizes({'output_res': '1280x720', 'upscaler': 'taa', 'preset': '3'}))


class LaunchPlanTests(unittest.TestCase):
    def test_startup_720p_dlss_reduces_guest_targets_and_keeps_output(self):
        with patch.dict('os.environ', {}, clear=True):
            plan = launch_plan({'output_res': '1280x720', 'upscaler': 'dlss',
                                'preset': '1', 'live_resolution': '0'})
        self.assertEqual(plan['mode'], 'startup')
        self.assertEqual(plan['render'], '854x480')
        self.assertEqual(plan['output'], '1280x720')
        self.assertEqual(plan['direct_memory_mb'], 9152)

    def test_live_and_taa_keep_native_guest_targets(self):
        with patch.dict('os.environ', {}, clear=True):
            for settings in ({'live_resolution': '1'}, {'upscaler': 'taa'}):
                with self.subTest(settings=settings):
                    plan = launch_plan({'output_res': '1280x720', 'preset': '1', **settings})
                    self.assertEqual(plan['mode'], 'live')
                    self.assertEqual(plan['render'], '')
                    self.assertIsNone(plan['direct_memory_mb'])

    def test_initial_cli_overrides_and_saved_restart_choices(self):
        settings = {'output_res': '1280x720', 'upscaler': 'off', 'preset': '0'}
        with patch.dict('os.environ', {'BB_UPSCALER': 'dlss', 'BB_UPSCALE_PRESET': '3'}, clear=True):
            self.assertEqual(launch_plan(settings)['render'], '640x360')
        with patch.dict('os.environ', {}, clear=True):
            self.assertEqual(launch_plan(settings)['render'], '1280x720')

    def test_explicit_render_remains_fixed_and_output_override_scales_automatic_scene(self):
        with patch.dict('os.environ', {}, clear=True):
            settings = {'upscaler': 'dlss', 'preset': '1', 'live_resolution': '1'}
            plan = launch_plan(settings, '800x450', '2560x1440')
            self.assertEqual((plan['render'], plan['output']), ('800x450', '2560x1440'))
            self.assertEqual(plan['mode'], 'startup')
            settings['live_resolution'] = '0'
            self.assertEqual(launch_plan(settings, output_override='1280x720')['render'], '854x480')

    def test_default_1080p_preserves_existing_live_path(self):
        with patch.dict('os.environ', {}, clear=True):
            self.assertEqual(launch_plan({'upscaler': 'dlss', 'preset': '1'})['render'], '')

    def test_high_resolution_avoids_scene_ui_size_collision(self):
        with patch.dict('os.environ', {}, clear=True):
            plan = launch_plan({'output_res': '3840x2160', 'upscaler': 'dlss', 'preset': '3'})
        self.assertEqual(plan['render'], '1916x1078')
        self.assertEqual(plan['output'], '3840x2160')


class DebugPatchTests(unittest.TestCase):
    def test_vertex_fix_is_opt_in_versioned_and_compatible_with_effects(self):
        self.assertEqual(effect_patches({}), [])
        self.assertEqual(effect_patches({'vertex_explosion_fix': '0'}), [])
        names = effect_patches({'vertex_explosion_fix': '1'})
        self.assertEqual(names, ['Disable Facegen'])
        writes = compile_patches(XML, names, '01.09', SEGMENTS)
        self.assertEqual(writes, [(0x020A63ED-EBOOT_BASE, bytes.fromhex('90E91D030000')),
                                  (0x01C49F26-EBOOT_BASE, b'\x00')])
        with self.assertRaisesRegex(ValueError, 'patches not found'):
            compile_patches(XML, names, '01.00', SEGMENTS)
        ours = {off+i: byte for off, data in writes for i, byte in enumerate(data)}
        # The option must not collide with FPS, resolution, or the other effect patches.
        for name in patches.FPS_PRESETS['uncap'] + [RESOLUTION_TEMPLATE] + [
                n for pair in patches.EFFECTS.values() for n in pair if n and n not in names]:
            for off, data in compile_patches(XML, [name], '01.09', SEGMENTS):
                for i, byte in enumerate(data):
                    if off+i in ours:
                        self.assertEqual(ours[off+i], byte, name)

    def test_camera_patch_is_optional_and_compatible_with_fps_and_debug_menu(self):
        self.assertEqual(effect_patches({'debug_camera': '0', 'debug_menu': '0'}), [])
        camera = effect_patches({'debug_camera': '1'})
        writes = compile_patches(XML, camera, '01.09', SEGMENTS)
        self.assertGreater(len(writes), 0)
        camera_bytes = {offset+i: byte for offset, data in writes for i, byte in enumerate(data)}
        for patch in ('Uncap FPS++', '60 FPS++', '90 FPS++', 'Restore Debug Menu (READ NOTES)'):
            for offset, data in compile_patches(XML, [patch], '01.09', SEGMENTS):
                for i, byte in enumerate(data):
                    if offset+i in camera_bytes:
                        self.assertEqual(camera_bytes[offset+i], byte, patch)

    def test_debug_menu_needs_both_fonts_where_the_game_reads_them(self):
        names = effect_patches({'debug_menu': '1'})
        menu = 'Restore Debug Menu (READ NOTES)'
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            self.assertEqual(validate_patch_requirements(['Restore Debug Camera'], game),
                             ['Restore Debug Camera'])
            # Without the fonts the patch is left out (the game would crash opening the menu).
            self.assertNotIn(menu, validate_patch_requirements(names, game))
            # The old instructions' folder: still left out, the game reads adhoc/font.
            old = game / 'dvdroot_ps4/font'
            old.mkdir(parents=True)
            (old / 'DbgFont14h.ccm').write_bytes(b'test')
            (old / 'DbgFont14h.tpf').write_bytes(b'test')
            self.assertNotIn(menu, validate_patch_requirements(names, game))
            # A mod's own case (Adhoc/Font/dbgfont14h.*) is found; an empty file is not enough.
            font = game / 'dvdroot_ps4/Adhoc/Font'
            font.mkdir(parents=True)
            (font / 'dbgfont14h.ccm').write_bytes(b'test')
            (font / 'dbgfont14h.tpf').touch()
            self.assertNotIn(menu, validate_patch_requirements(names, game))
            (font / 'dbgfont14h.tpf').write_bytes(b'test')
            self.assertIn(menu, validate_patch_requirements(names, game))

    def test_conflicting_enemy_control_patch_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'conflicts with Enemy Control'):
            validate_patch_requirements(['Enemy Control', 'Restore Debug Camera'], Path('.'))


EXTERNAL = """<?xml version="1.0"?>
<Patch>
  <TitleID><ID>CUSA03173</ID><ID>CUSA00207</ID></TitleID>
  <Metadata Title="Bloodborne" Name="On" Author="x" PatchVer="1.0" AppVer="01.09" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00401000" Value="9090"/></PatchList>
  </Metadata>
  <Metadata Title="Bloodborne" Name="Off" Author="x" PatchVer="1.0" AppVer="01.09" AppElf="eboot.bin">
    <PatchList><Line Type="bytes32" Address="0x00402000" Value="0x12345678"/></PatchList>
  </Metadata>
  <Metadata Title="Bloodborne" Name="Mask" Author="x" PatchVer="1.0" AppVer="01.09" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="mask" Value="90 ?? 90" Offset="0"/></PatchList>
  </Metadata>
  <Metadata Title="Bloodborne" Name="Old" Author="x" PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00403000" Value="90"/></PatchList>
  </Metadata>
</Patch>"""


class ExternalPatchTests(unittest.TestCase):
    def test_selection_and_unsupported_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'extra.xml').write_text(EXTERNAL)
            (folder / 'other.xml').write_text(EXTERNAL.replace('CUSA03173', 'CUSA99999')
                                              .replace('CUSA00207', 'CUSA99998'))
            (folder / 'broken.xml').write_text('<Patch>')
            found = external_patches(folder)
            self.assertEqual([key for key, _, _ in found],
                             ['extra.xml/On', 'extra.xml/Off', 'extra.xml/Mask'])
            # The file's isEnabled; the mask patch is skipped as a whole.
            writes = compile_external(external_selection(found, None), SEGMENTS)
            self.assertEqual(writes, [(0x1000, bytes.fromhex('9090'))])
            config = folder / 'patches.json'
            config.write_text('{"enabled": ["extra.xml/Off"], "disabled": ["extra.xml/On"]}')
            writes = compile_external(external_selection(found, config), SEGMENTS)
            self.assertEqual(writes, [(0x2000, (0x12345678).to_bytes(4, 'little'))])

    def test_built_in_file_is_not_external(self):
        self.assertEqual(external_patches(XML.parent), [])


class IntelTonemapTests(unittest.TestCase):
    def cpuinfo(self, vendor):
        path = Path(tempfile.mkdtemp()) / 'cpuinfo'
        path.write_text(f'processor\t: 0\nvendor_id\t: {vendor}\nmodel name\t: x\n')
        return str(path)

    def test_on_for_intel_off_for_amd(self):
        self.assertTrue(patches.intel_tonemap_fix({}, self.cpuinfo('GenuineIntel')))
        self.assertFalse(patches.intel_tonemap_fix({}, self.cpuinfo('AuthenticAMD')))

    def test_environment_forces_it(self):
        amd, intel = self.cpuinfo('AuthenticAMD'), self.cpuinfo('GenuineIntel')
        self.assertTrue(patches.intel_tonemap_fix({'BB_INTEL_TONEMAP_FIX': '1'}, amd))
        self.assertFalse(patches.intel_tonemap_fix({'BB_INTEL_TONEMAP_FIX': '0'}, intel))

    def test_patch_exists_for_109(self):
        xml = ET.parse(ROOT / 'patches/Bloodborne.xml')
        names = [m.get('Name') for m in xml.iter('Metadata')]
        self.assertIn(patches.INTEL_TONEMAP, names)


if __name__ == '__main__':
    unittest.main()
