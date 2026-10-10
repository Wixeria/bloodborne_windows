"""Drive only the Rust launcher's own test entry point, using a copied save and FG off."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "launcher"))
import windows_model as model


def hashes(path):
    return {str(p.relative_to(path)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in path.rglob("*") if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--executable", type=Path, default=ROOT / "out/windows-launcher-rust-target/debug/BloodborneLauncher.exe")
    parser.add_argument("--window-mode", choices=tuple(model.WINDOW_MODES.values()), default="windowed")
    parser.add_argument("--runtime", type=Path, default=ROOT / "out/windows-fg-game")
    parser.add_argument("--live-resolution", choices=("0", "1"), default="0")
    parser.add_argument("--preset", choices=tuple(str(i) for i in range(5)), default="0")
    parser.add_argument("--stop-timeout", type=int, default=90)
    args = parser.parse_args()
    case = args.output.resolve()
    if case.exists() or not case.is_relative_to(ROOT / "out"):
        raise ValueError("Use a new test directory within workspace out/.")
    source = ROOT / "out/windows-play-user/savedata"
    before = hashes(source)
    ini = model.resolve(ROOT, model.preferences()["output"]) / "bbport.ini"
    original = ini.read_bytes() if ini.exists() else None
    preferences = ROOT / "out/windows-launcher/settings.json"
    original_prefs = preferences.read_bytes() if preferences.exists() else None
    case.mkdir(parents=True)
    runtime = args.runtime.resolve()
    if not runtime.is_relative_to(ROOT / "out"):
        raise ValueError("Use a runtime within workspace out/.")
    (case / "runtime-path.txt").write_text(str(runtime), encoding="utf-8")
    shutil.copytree(source, case / "user/savedata")
    cache = source.parent / "cache"
    if cache.exists():
        shutil.copytree(cache, case / "user/cache")
    values = {**model.defaults(), **model.read_ini(ini)}
    values.update(upscaler="dlss", output_res="1280x720", frame_generation="0",
                  fg_multiplier="2", skip_intro="1", window_mode=args.window_mode,
                  live_resolution=args.live_resolution, preset=args.preset)
    model.atomic_write(case / "bbport.ini", "".join(f"{k}={v}\n" for k,v in values.items()))
    pad = case / "pad.txt"
    pad.write_text("")
    log = (case / "launcher-ui.log").open("wb")
    process = subprocess.Popen([str(args.executable.resolve()), "--game-smoke", str(case)],
                               stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
    reached = False
    failure = None
    started = time.monotonic()
    def text():
        return "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in (case / "logs").glob("*.log"))
    def until(needle, seconds):
        end = time.monotonic()+seconds
        while needle not in text():
            if process.poll() is not None:
                raise RuntimeError("Rust UI exited before the scene was loaded.")
            if time.monotonic()>end:
                raise TimeoutError(needle)
            time.sleep(.15)
    try:
        until("UI: native composition", 90)
        time.sleep(3)
        for delay in (4,4,1):
            pad.write_text("cross")
            time.sleep(.7)
            pad.write_text("")
            time.sleep(delay)
        until("Camera motion: scene render size", 90)
        reached = True
        time.sleep(2)
        changed = model.read_ini(case / "bbport.ini")
        changed["fg_multiplier"] = "4"
        model.atomic_write(case / "bbport.ini", "".join(f"{k}={v}\n" for k,v in changed.items()))
        (case / "stop-session").write_text("stop")
        if process.wait(timeout=args.stop_timeout) != 0:
            raise RuntimeError("Rust UI returned an error.")
        ui = json.loads((case / "ui-summary.json").read_text())
        if ui["exit_code"] != 0 or not ui["settings_reloaded"]:
            raise RuntimeError(f"Lifecycle result: {ui}")
    except Exception as error:
        failure = str(error)
    finally:
        pad.write_text("")
        (case / "stop-session").write_text("stop")
        if process.poll() is None:
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                failure = (failure or "")+" UI remains active; inspect the test logs."
        log.close()
        if original_prefs is None:
            preferences.unlink(missing_ok=True)
        else:
            preferences.write_bytes(original_prefs)
    ui_path = case / "ui-summary.json"
    ui = json.loads(ui_path.read_text()) if ui_path.exists() else {}
    unchanged = before == hashes(source)
    ini_unchanged = (ini.read_bytes() if ini.exists() else None) == original
    result = dict(gameplay_reached=reached, settings_reloaded=ui.get("settings_reloaded", False),
                  source_save_unchanged=unchanged, source_ini_unchanged=ini_unchanged,
                  failure=failure, fg="off", window_mode=args.window_mode,
                  live_resolution=args.live_resolution, preset=args.preset,
                  elapsed_seconds=round(time.monotonic()-started,2))
    (case / "summary.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2), flush=True)
    return int(bool(failure) or not all([reached, unchanged, ini_unchanged, result["settings_reloaded"]]))


if __name__ == "__main__":
    raise SystemExit(main())
