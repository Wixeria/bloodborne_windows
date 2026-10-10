"""Exercise the real Tk launcher with a copied save; FG stays off for this lifecycle test."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import time
import tkinter as tk

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "launcher"))
import windows_model as model
from windows_launcher import Launcher


def hashes(path):
    return {str(p.relative_to(path)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in path.rglob("*") if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-user", type=Path, default=ROOT / "out/windows-play-user")
    args = parser.parse_args()
    case, source = args.output.resolve(), args.source_user.resolve()
    if not case.is_relative_to((ROOT / "out").resolve()) or case.exists():
        raise ValueError("Use a new test directory within workspace out/.")
    before = hashes(source / "savedata")
    case.mkdir(parents=True)
    shutil.copytree(source / "savedata", case / "user/savedata")
    if (source / "cache").exists():
        shutil.copytree(source / "cache", case / "user/cache")
    preferences_path = ROOT / "out/windows-launcher/settings.json"
    previous_preferences = preferences_path.read_bytes() if preferences_path.exists() else None
    settings = model.preferences()
    original_ini = model.resolve(ROOT, settings["output"]) / "bbport.ini"
    original_bytes = original_ini.read_bytes() if original_ini.exists() else None
    values = {**model.defaults(), **model.read_ini(original_ini)}
    values.update(upscaler="dlss", preset="0", output_res="1280x720",
                  frame_generation="0", fg_multiplier="2", skip_intro="1")
    model.atomic_write(case / "bbport.ini", "".join(f"{k}={v}\n" for k, v in values.items()))
    pad = case / "pad.txt"
    pad.write_text("")
    real_environment = model.launch_environment
    def environment(settings, stop):
        return {**real_environment(settings, stop), "BB_PAD_FILE": str(pad),
                "BB_PAD_SCRIPT_ONLY": "1", "BB_FRAME_STATS": "0", "BB_GPU_PROFILE": "0"}
    model.launch_environment = environment
    root = tk.Tk()
    app = Launcher(root)
    app.launch_vars["output"].set(str(case))
    app.launch_vars["user"].set(str(case / "user"))
    app.launch_vars["engine"].set("menu-sm86")
    app.launch_vars["build"].set(str(ROOT / "out/windows-fg-game"))
    app.launch_vars["present"].set("Immediate")
    app.launch_vars["window"].set("1280x720")
    app.load_profile(force=True)
    started = time.monotonic()
    failure = None
    reached_gameplay = False
    reloaded = False
    runner_pid = None
    try:
        app.launch()
        if not app.process:
            raise RuntimeError("Launcher did not start the game runner.")
        runner_pid = app.process.pid
        def text():
            paths = [app.log_path, *sorted(app.game_log_prefix.parent.glob(app.game_log_prefix.name + "-*.log"))]
            return "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in paths)
        def pump(seconds):
            deadline = time.monotonic()+seconds
            while time.monotonic() < deadline:
                root.update()
                if app.process is None:
                    raise RuntimeError("Runner exited before the test completed.")
                time.sleep(.025)
        def until(needle, seconds):
            deadline = time.monotonic()+seconds
            while needle not in text():
                if time.monotonic() > deadline:
                    raise TimeoutError(needle)
                pump(.2)
        until("UI: native composition", 90)
        pump(3)
        for delay in (4, 4, 1):
            pad.write_text("cross")
            pump(.7)
            pad.write_text("")
            pump(delay)
        until("Camera motion: scene render size", 90)
        reached_gameplay = True
        pump(2)
        # Simulate an Insert-written change and prove the launcher reloads it on exit.
        changed = model.read_ini(case / "bbport.ini")
        changed["fg_multiplier"] = "4"
        model.atomic_write(case / "bbport.ini", "".join(f"{k}={v}\n" for k, v in changed.items()))
        app.stop()
        deadline = time.monotonic()+30
        while app.process is not None and time.monotonic() < deadline:
            root.update()
            time.sleep(.025)
        if app.process is not None:
            raise TimeoutError("Stop request did not finish the game runner.")
        reloaded = app.ini_vars["fg_multiplier"].get() == "4"
        if not reloaded:
            raise RuntimeError("Launcher did not reload the Insert-style ini change.")
    except Exception as error:
        failure = str(error)
    finally:
        pad.write_text("")
        if app.process is not None:
            app.stop()
            deadline = time.monotonic()+30
            while app.process is not None and time.monotonic()<deadline:
                root.update()
                time.sleep(.025)
            if app.process is not None:
                failure = (failure or "") + " Runner remains active; see session log."
        app.closing = True
        root.destroy()
        # Restore only this test's launcher preferences; never alter the user's ini or saves.
        if previous_preferences is None:
            preferences_path.unlink(missing_ok=True)
        else:
            preferences_path.write_bytes(previous_preferences)
    unchanged = before == hashes(source / "savedata")
    original_unchanged = (original_ini.read_bytes() if original_ini.exists() else None) == original_bytes
    summary = {"gameplay_reached": reached_gameplay, "settings_reloaded": reloaded,
               "source_save_unchanged": unchanged, "source_ini_unchanged": original_unchanged,
               "failure": failure, "elapsed_seconds": time.monotonic()-started,
               "log": str(app.log_path), "runner_pid": runner_pid, "fg": "off"}
    (case / "summary.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2), flush=True)
    return int(bool(failure) or not (unchanged and original_unchanged and reloaded and reached_gameplay))


if __name__ == "__main__":
    raise SystemExit(main())
