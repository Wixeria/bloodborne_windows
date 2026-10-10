"""Launcher regressions: persistence, shell-free paths, controller bindings and save backups."""
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "launcher"))
import windows_model as model


def write_sfo(game, version="01.09", title="CUSA03173"):
    (game / "sce_sys").mkdir(parents=True)
    keys = b"TITLE_ID\0APP_VER\0"
    values = title.encode() + b"\0" + version.encode() + b"\0"
    header = struct.pack("<4sIIII", b"\0PSF", 0x101, 52, 52 + len(keys), 2)
    entries = struct.pack("<HHIII", 0, 0x204, len(title)+1, len(title)+1, 0)
    entries += struct.pack("<HHIII", 9, 0x204, len(version)+1, len(version)+1, len(title)+1)
    (game / "sce_sys/param.sfo").write_bytes(header + entries + keys + values)
    (game / "eboot.bin").write_bytes(b"test fixture")


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bb-launcher-")
        self.root = Path(self.temp.name)
        self.game = self.root / "O'Brien & game 游戏"
        write_sfo(self.game)
        self.build = self.root / "exe"
        self.build.mkdir()
        for name in ("bb-game.exe", "bb-probe.exe", "nvngx_dlss.dll", "sl.interposer.dll", ".bbport-dlssg-game"):
            (self.build / name).write_bytes(b"fixture")
        self.settings = {"game": str(self.game), "patch": "", "use_patch": False,
                         "build": str(self.build), "output": str(self.root / "profile"),
                         "user": str(self.root / "saves"), "engine": "menu-sm86",
                         "window": "1280x720", "present": "Immediate", "fps_patch": "uncap",
                         "fps_limit": 120, "fullscreen": False}
        self.values = model.defaults()
        self.values.update(upscaler="dlss", frame_generation="1", fg_multiplier="6")

    def tearDown(self):
        self.temp.cleanup()

    def test_metadata_matches_shared_binding_defaults(self):
        actions = model.binding_actions()
        self.assertEqual(len(actions), 17)
        self.assertEqual(next(a for a in actions if a["key"] == "r1")["defaults"], (32, -1))
        self.assertEqual(next(a for a in actions if a["key"] == "circle")["defaults"], (225, 41))

    def test_ini_preserves_overlay_changes_and_unknown_keys(self):
        path = self.root / "bbport.ini"
        path.write_text("# keep me\nupscaler=dlss\nfg_multiplier=2\ncustom_future=abc\n", encoding="utf-8")
        baseline = model.read_ini(path)
        loaded = {**model.defaults(), **baseline}
        edited = dict(loaded, sharpness="0.50")
        path.write_text("# keep me\nupscaler=dlss\nfg_multiplier=6\ncustom_future=abc\n", encoding="utf-8")
        result = model.save_ini(path, edited, baseline, loaded)
        self.assertEqual(result["fg_multiplier"], "6")
        self.assertEqual(result["sharpness"], "0.50")
        self.assertEqual(result["custom_future"], "abc")
        self.assertTrue(path.read_text().startswith("# keep me\n"))

    def test_conflicting_ini_edits_do_not_write(self):
        path = self.root / "bbport.ini"
        path.write_text("fg_multiplier=2\n")
        baseline = model.read_ini(path)
        path.write_text("fg_multiplier=6\n")
        with self.assertRaisesRegex(ValueError, "Reload"):
            model.save_ini(path, {"fg_multiplier": "4"}, baseline)
        self.assertEqual(model.read_ini(path)["fg_multiplier"], "6")

    def test_new_external_ini_key_is_preserved(self):
        path = self.root / "bbport.ini"
        loaded = model.defaults()
        path.write_text("frame_generation=1\nfg_multiplier=6\n")
        result = model.save_ini(path, loaded, {}, loaded)
        self.assertEqual(result["frame_generation"], "1")
        self.assertEqual(result["fg_multiplier"], "6")

    def test_launch_paths_are_literal_arguments_without_graphics_overrides(self):
        model.validate(self.root, self.settings, self.values)
        command = model.launch_command(self.root, self.settings)
        self.assertEqual(command[command.index("-GameDirectory")+1], str(self.game))
        self.assertIn("-BaseOnly", command)
        self.assertIn("menu-sm86", command)
        self.assertNotIn("-Command", command)
        for key in ("-Upscaler", "-UpscalePreset", "-OutputResolution", "-ShowFps", "-SkipIntro"):
            self.assertNotIn(key, command)
        log = self.root / "O'Brien & 游戏 log"
        command = model.launch_command(self.root, self.settings, game_log_prefix=log)
        self.assertEqual(command[command.index("-GameLogPrefix")+1], str(log))

    def test_stale_runtime_environment_is_removed(self):
        with patch.dict(os.environ, {"BB_FRAME_GENERATION": "on", "BB_CONFIG": "stale",
                                     "bb_render_res": "stale", "NORMAL_TEST": "keep"}):
            env = model.launch_environment(self.settings, self.root / "stop")
        self.assertNotIn("BB_CONFIG", env)
        self.assertNotIn("BB_FRAME_GENERATION", env)
        self.assertNotIn("bb_render_res", env)
        self.assertEqual(env["NORMAL_TEST"], "keep")
        self.assertEqual(env["BB_STOP_FILE"], str(self.root / "stop"))

    def test_fg_constraints_and_numeric_validation(self):
        for key, value in (("upscaler", "fsr3"), ("fg_multiplier", "9"),
                           ("mouse_sensitivity", "nan"), ("sharpness", "inf")):
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    model.validate(self.root, self.settings, {**self.values, key: value})
        with self.assertRaisesRegex(ValueError, "Immediate"):
            model.validate(self.root, {**self.settings, "present": "Mailbox"}, self.values)
        with self.assertRaisesRegex(ValueError, "FG runtime"):
            model.validate(self.root, {**self.settings, "engine": "off"}, self.values)

    def test_source_game_and_save_locations_are_protected(self):
        for key in ("output", "build", "user"):
            with self.subTest(key=key):
                with self.assertRaisesRegex(ValueError, "outside the game"):
                    model.validate(self.root, {**self.settings, key: str(self.game / "out")}, self.values)
        with self.assertRaisesRegex(ValueError, "separate"):
            model.validate(self.root, {**self.settings, "output": self.settings["user"]}, self.values)

    def test_update_title_and_version(self):
        write_sfo(self.game.parent / "patch", title="CUSA00207")
        settings = {**self.settings, "use_patch": True, "patch": str(self.game.parent / "patch")}
        with self.assertRaisesRegex(ValueError, "match"):
            model.validate(self.root, settings, self.values)
        (self.game.parent / "patch/sce_sys/param.sfo").unlink()
        with self.assertRaisesRegex(ValueError, "Cannot read"):
            model.validate(self.root, settings, self.values)

    def test_binding_move_reserved_keys_and_unbinding(self):
        values = model.defaults()
        model.rebind(values, "bind_cross_alt", -3)
        self.assertEqual(values["bind_cross_alt"], "-3")
        self.assertEqual(values["bind_l2_alt"], "0")
        for code in (65, 68, 73, -6, 512):
            with self.assertRaises(ValueError):
                model.rebind(values, "bind_cross", code)
        model.rebind(values, "bind_cross", 0)
        self.assertEqual(values["bind_cross"], "0")
        self.assertEqual(values["bind_r3"], "6")

    def test_physical_key_capture(self):
        self.assertEqual(model.key_event_code("Shift_R", 16), 229)
        self.assertEqual(model.key_event_code("KP_0", 96), 98)
        self.assertEqual(model.key_event_code("unfamiliar_layout_letter", 65), 4)
        self.assertEqual(model.key_event_code("Insert", 45), 73)
        self.assertEqual(model.key_event_code("F8", 119), 65)
        self.assertEqual(model.key_event_code("F11", 122), 68)
        self.assertIsNone(model.key_event_code("unknown", 0))

    def test_save_import_keeps_source_and_backs_up_destination(self):
        source = self.root / "shad/CUSA00207/SPRJ0005"
        source.mkdir(parents=True)
        (source / "userdata0000").write_bytes(b"new save")
        target, backup = model.import_save(source, self.root / "user")
        self.assertIsNone(backup)
        self.assertEqual((source / "userdata0000").read_bytes(), b"new save")
        (target / "userdata0000").write_bytes(b"old save")
        target, backup = model.import_save(source, self.root / "user")
        self.assertEqual((backup / "userdata0000").read_bytes(), b"old save")
        self.assertEqual((target / "userdata0000").read_bytes(), b"new save")
        self.assertFalse(any(target.parent.glob(".save-import-*")))
        with self.assertRaisesRegex(ValueError, "separate"):
            model.import_save(target, self.root / "user")

    def test_corrupt_metadata_has_actionable_error(self):
        (self.game / "sce_sys/param.sfo").write_bytes(b"short")
        with self.assertRaisesRegex(ValueError, "Cannot read"):
            model.metadata(self.game)


if __name__ == "__main__":
    unittest.main()
