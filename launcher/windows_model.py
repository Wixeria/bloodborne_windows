# SPDX-License-Identifier: GPL-2.0-or-later
"""Windows launcher settings, binding metadata and shell-free launch preparation."""
from __future__ import annotations

from datetime import datetime
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import stat
import struct

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from prepare import sfo

RESOLUTIONS = ("1280x720", "1920x1080", "2560x1440", "3840x2160")
WINDOW_MODES = {"Windowed": "windowed", "Borderless Fullscreen": "borderless", "Fullscreen": "fullscreen"}
UPSCALERS = {"Off": "off", "DLSS (NVIDIA)": "dlss", "FSR 3.1": "fsr3",
             "FSR 4 (INT8)": "fsr4", "FSR 4.1.1": "fsr411", "Native TAA": "taa"}
PRESETS = ("Native AA", "Quality", "Balanced", "Performance", "Ultra Performance")
ENGINES = {"Standard Windows": "off", "Experimental SM86 (RTX 20/30)": "menu-sm86",
           "Official NVIDIA FG": "menu"}
EFFECTS = (
    ("effect_chromatic_aberration", "Chromatic aberration", True),
    ("effect_dof", "Depth of field", True),
    ("effect_motion_blur", "Motion blur", True),
    ("effect_ssao", "SSAO", True),
    ("effect_game_aa", "In-game antialiasing", True),
    ("effect_dynamic_shadows", "Dynamic light shadows", True),
    ("effect_ssr", "Screen-space reflections", False),
    ("vertex_explosion_fix", "Vertex explosion fix (disable FaceGen)", False),
    ("skip_intro", "Skip intro videos", False),
    ("debug_camera", "Free camera (Cross + L3)", False),
    ("debug_menu", "Debug menu (requires game font files)", False),
)

# SDL's stable USB HID scancodes, also used by the runtime's ini.
KEY_CODES = {chr(65 + i): 4 + i for i in range(26)}
KEY_CODES.update({str(i): 29 + i for i in range(1, 10)})
KEY_CODES.update({"0": 39, "Enter": 40, "Escape": 41, "Backspace": 42, "Tab": 43,
                 "Space": 44, "-": 45, "=": 46, "[": 47, "]": 48, "\\": 49,
                 ";": 51, "'": 52, "Grave": 53, ",": 54, ".": 55, "/": 56,
                 "Caps Lock": 57, "Print Screen": 70, "Scroll Lock": 71, "Pause": 72,
                 "Home": 74, "Page Up": 75, "Delete": 76, "End": 77, "Page Down": 78,
                 "Right": 79, "Left": 80, "Down": 81, "Up": 82,
                 "Num Lock": 83, "Numpad /": 84, "Numpad *": 85, "Numpad -": 86,
                 "Numpad +": 87, "Numpad Enter": 88, "Numpad 0": 98, "Numpad .": 99,
                 "Menu": 101, "Left Ctrl": 224, "Left Shift": 225, "Left Alt": 226,
                 "Left Win": 227, "Right Ctrl": 228, "Right Shift": 229,
                 "Right Alt": 230, "Right Win": 231})
KEY_CODES.update({f"F{i}": 57 + i for i in range(1, 13) if i not in (8, 11)})
KEY_CODES.update({f"Numpad {i}": 88 + i for i in range(1, 10)})
BINDING_CHOICES = {"Unbound": 0, **KEY_CODES, "Mouse Left": -1, "Mouse Middle": -2,
                   "Mouse Right": -3, "Mouse Side 1": -4, "Mouse Side 2": -5}
SDL_NAMES = {f"SDL_SCANCODE_{k}": v for k, v in KEY_CODES.items()
             if len(k) == 1 and k.isalnum()}
SDL_NAMES.update({"SDL_SCANCODE_SPACE": 44, "SDL_SCANCODE_LSHIFT": 225,
                  "SDL_SCANCODE_ESCAPE": 41, "SDL_SCANCODE_RETURN": 40,
                  "SDL_SCANCODE_TAB": 43, "SDL_SCANCODE_BACKSPACE": 42,
                  "-SDL_BUTTON_X2": -5, "-SDL_BUTTON_X1": -4,
                  "-SDL_BUTTON_RIGHT": -3, "-SDL_BUTTON_LEFT": -1,
                  "-SDL_BUTTON_MIDDLE": -2, "0": 0})


def binding_actions(root: Path = ROOT) -> list[dict]:
    """Read descriptions/defaults from the runtime's shared metadata."""
    text = (root / "src/runtime_pad_bindings.h").read_text(encoding="utf-8")
    pattern = r'X\(\w+, "([^"]+)", "([^"]+)", "([^"]+)", 0x[0-9a-f]+, ([\w-]+), ([\w-]+)\)'
    result = [{"key": k, "label": label, "action": action,
               "defaults": (SDL_NAMES[a], SDL_NAMES[b])}
              for k, label, action, a, b in re.findall(pattern, text)]
    if len(result) != 17:
        raise ValueError("Controller metadata is missing or incompatible with this launcher.")
    return result


def defaults(root: Path = ROOT) -> dict[str, str]:
    values = {"upscaler": "off", "preset": "0", "output_res": "1920x1080",
              "live_resolution": "0", "window_mode": "windowed", "sharpen": "1", "sharpness": "0.30",
              "jitter": "1", "object_motion": "1", "show_fps": "1",
              "frame_generation": "0", "fg_multiplier": "2", "reflex": "on",
              "keyboard_mouse": "1", "mouse_capture": "1", "mouse_invert_y": "0",
              "mouse_sensitivity": "1.00", "dpad_camera": "0", "model_lod": "0"}
    values.update({key: str(int(enabled)) for key, _, enabled in EFFECTS})
    for action in binding_actions(root):
        for suffix, code in zip(("", "_alt"), action["defaults"]):
            values[f'bind_{action["key"]}{suffix}'] = str(code)
    return values


def read_ini(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    result = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        key, sep, value = line.partition("=")
        if sep and not key.lstrip().startswith(("#", ";")):
            result[key.strip()] = value.strip()
    return result


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def save_ini(path: Path, values: dict[str, str], baseline: dict[str, str],
             loaded: dict[str, str] | None = None) -> dict[str, str]:
    """Merge edited keys with disk state; refuse conflicting concurrent edits."""
    current = read_ini(path)
    reference = baseline if loaded is None else loaded
    updates = {k: str(v) for k, v in values.items()
               if k not in current or str(v) != reference.get(k)}
    for key, value in updates.items():
        if (current.get(key) != baseline.get(key) and key in current
                and current.get(key) != value):
            raise ValueError(f"{key} changed outside the launcher. Reload settings before saving.")
    lines = path.read_text(encoding="utf-8-sig").splitlines() if path.exists() else []
    output, written = [], set()
    for line in lines:
        key, sep, _ = line.partition("=")
        key = key.strip()
        if sep and key in updates:
            if key not in written:
                output.append(f"{key}={updates[key]}")
                written.add(key)
        else:
            output.append(line)
    output.extend(f"{key}={value}" for key, value in updates.items() if key not in written)
    atomic_write(path, "\n".join(output) + "\n")
    return read_ini(path)


def rebind(values: dict[str, str], key: str, code: int) -> None:
    if not (-5 <= code < 512) or code in (65, 68, 73):
        raise ValueError("Insert, F8 and F11 are reserved. Choose a supported key or mouse button.")
    if not key.startswith("bind_"):
        raise ValueError("Unknown controller binding.")
    if code:
        for other, existing in list(values.items()):
            if other.startswith("bind_") and other != key and existing == str(code):
                values[other] = "0"
    values[key] = str(code)


def binding_name(code: int) -> str:
    return next((name for name, value in BINDING_CHOICES.items() if value == code),
                f"Scancode {code}")


def key_event_code(keysym: str, keycode: int) -> int | None:
    """Tk Windows virtual keys to SDL physical IDs; named modifiers stay distinct."""
    special = {"Return": 40, "Escape": 41, "BackSpace": 42, "Tab": 43, "space": 44,
               "Insert": 73, "F8": 65, "F11": 68, "Shift_L": 225, "Shift_R": 229,
               "Control_L": 224, "Control_R": 228, "Alt_L": 226, "Alt_R": 230,
               "Win_L": 227, "Win_R": 231, "Super_L": 227, "Super_R": 231,
               "Prior": 75, "Next": 78, "Caps_Lock": 57, "Num_Lock": 83,
               "KP_Enter": 88, "KP_Add": 87, "KP_Subtract": 86, "KP_Multiply": 85,
               "KP_Divide": 84, "KP_Decimal": 99}
    if keysym in special:
        return special[keysym]
    if keysym.startswith("KP_") and keysym[3:].isdigit():
        return KEY_CODES.get("Numpad " + keysym[3:])
    if keysym in KEY_CODES:
        return KEY_CODES[keysym]
    if 65 <= keycode <= 90:
        return 4 + keycode - 65
    if 48 <= keycode <= 57:
        return KEY_CODES[chr(keycode)]
    if 96 <= keycode <= 105:
        return KEY_CODES[f"Numpad {keycode-96}"]
    return {186: 51, 187: 46, 188: 54, 189: 45, 190: 55, 191: 56,
            192: 53, 219: 47, 220: 49, 221: 48, 222: 52}.get(keycode)


def preferences(root: Path = ROOT) -> dict:
    local_path = root / "out/windows-local.json"
    local = json.loads(local_path.read_text(encoding="utf-8-sig")) if local_path.exists() else {}
    profile = root / "out/windows-fg-play"
    if not (profile / "bbport.ini").exists():
        profile = root / "out/windows-game"
    patch = local.get("patchDirectory", "")
    if patch and not (Path(patch) / "sce_sys/param.sfo").is_file():
        patch = ""
    fg_available = (root / "out/windows-fg-game/.bbport-dlssg-game").is_file()
    result = {"game": local.get("gameDirectory", ""), "patch": patch,
              "use_patch": bool(patch), "user": str(root / "out/windows-play-user"),
              "output": str(profile), "engine": "menu-sm86" if fg_available else "off",
              "build": str(root / ("out/windows-fg-game" if fg_available else "out/windows")),
              "window": "1280x720" if fg_available else "1920x1080",
              "present": "Immediate" if fg_available else "Mailbox",
              "fps_patch": "uncap", "fps_limit": 120, "fullscreen": False}
    saved = root / "out/windows-launcher/settings.json"
    if saved.exists():
        value = json.loads(saved.read_text(encoding="utf-8"))
        result.update({k: v for k, v in value.items() if k in result})
    return result


def resolve(root: Path, value: str) -> Path:
    if not str(value).strip():
        raise ValueError("A required folder is empty.")
    path = Path(value).expanduser()
    return (path if path.is_absolute() else root / path).resolve()


def metadata(game: Path) -> dict:
    try:
        return sfo((game / "sce_sys/param.sfo").read_bytes())
    except (OSError, ValueError, IndexError, struct.error) as error:
        raise ValueError(f"Cannot read the game version in {game}: {error}") from error


def validate(root: Path, settings: dict, values: dict[str, str], *, launching: bool = True) -> None:
    game, output = resolve(root, settings["game"]), resolve(root, settings["output"])
    user = resolve(root, settings["user"])
    if output == user or output.is_relative_to(user):
        raise ValueError("Choose an output folder separate from the save-data root.")
    for path in (output, user, resolve(root, settings["build"])):
        if path == game or path.is_relative_to(game):
            raise ValueError("Output, save and executable folders must be outside the game dump.")
    if values["upscaler"] not in UPSCALERS.values() or values["preset"] not in map(str, range(5)):
        raise ValueError("Choose a supported upscaler and preset.")
    if values["output_res"] not in RESOLUTIONS or settings["window"] not in RESOLUTIONS:
        raise ValueError("Choose a supported output and window resolution.")
    if values["live_resolution"] not in ("0", "1", "auto"):
        raise ValueError("Choose a supported resolution-change mode.")
    if values["window_mode"] not in WINDOW_MODES.values():
        raise ValueError("Choose a supported window mode.")
    if settings["engine"] not in ENGINES.values() or settings["present"] not in ("Immediate", "Mailbox", "Fifo"):
        raise ValueError("Choose a supported runtime and presentation mode.")
    if settings["fps_patch"] not in ("uncap", "60", "90", "preserve"):
        raise ValueError("Choose a supported FPS patch.")
    try:
        if not 0 <= int(settings["fps_limit"]) <= 480 or not 2 <= int(values["fg_multiplier"]) <= 6:
            raise ValueError()
        if not .1 <= float(values["mouse_sensitivity"]) <= 5 or not 0 <= float(values["sharpness"]) <= 2:
            raise ValueError()
    except (TypeError, ValueError):
        raise ValueError("FPS limit: 0-480; FG: 2X-6X; mouse sensitivity: 0.1-5; sharpness: 0-2.") from None
    for key, value in values.items():
        if key.startswith("bind_"):
            try:
                code = int(value)
            except ValueError:
                raise ValueError(f"Invalid binding: {key}") from None
            if not -5 <= code < 512 or code in (65, 68, 73):
                raise ValueError(f"Invalid or reserved binding: {key}")
    fg = values["frame_generation"] == "1"
    if fg:
        if settings["engine"] == "off":
            raise ValueError("Select an FG runtime in Graphics before enabling Frame Generation.")
        if values["upscaler"] not in ("dlss", "fsr4", "fsr411"):
            raise ValueError("Frame Generation currently needs DLSS or FSR4 scene inputs.")
    if settings["engine"] != "off" and settings["present"] != "Immediate":
        raise ValueError("This FG runtime requires Immediate (allow tearing) presentation.")
    if settings["use_patch"]:
        patch = resolve(root, settings["patch"])
        if output == patch or output.is_relative_to(patch) or user == patch or user.is_relative_to(patch):
            raise ValueError("Output and save folders must be outside the update dump.")
        if launching:
            update = metadata(patch)
            base = metadata(game)
            if update.get("TITLE_ID") != base.get("TITLE_ID") or update.get("APP_VER") != "01.09":
                raise ValueError("The separate update must match the game title and be version 01.09.")
    if not launching:
        return
    if not (game / "eboot.bin").is_file():
        raise ValueError("Select the game folder containing eboot.bin and sce_sys.")
    info = metadata(game)
    if info.get("TITLE_ID") != "CUSA03173":
        raise ValueError("This runner targets CUSA03173. Select that game dump.")
    if info.get("APP_VER") != "01.09" and not settings["use_patch"]:
        raise ValueError("The game must be 01.09, or select its separate 01.09 update.")
    build = resolve(root, settings["build"])
    if not (build / "bb-game.exe").is_file() or not (build / "bb-probe.exe").is_file():
        raise ValueError("The executable folder needs bb-game.exe and bb-probe.exe. Build the renderer first.")
    if settings["engine"] != "off":
        if not (build / "sl.interposer.dll").is_file():
            raise ValueError("Prepare an FG runtime with tools/windows/prepare_dlssg_game.py first.")
        if settings["engine"] == "menu-sm86" and not (build / ".bbport-dlssg-game").is_file():
            raise ValueError("SM86 requires the isolated runtime prepared by prepare_dlssg_game.py.")
        if settings["engine"] == "menu" and (build / "version.dll").is_file():
            raise ValueError("This folder contains the SM86 proxy. Select SM86 or prepare an official runtime.")
    if values["upscaler"] == "dlss" and not (build / "nvngx_dlss.dll").is_file():
        raise ValueError("The executable folder is missing nvngx_dlss.dll for Super Resolution.")


def launch_command(root: Path, settings: dict, *, game_log_prefix: Path | None = None) -> list[str]:
    """Arguments only: never shell interpolation or -Command."""
    powershell = shutil.which("powershell.exe") or "powershell.exe"
    command = [powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
               str(root / "run-windows.ps1"), "-Gpu", "-SkipBuild",
               "-GameDirectory", str(resolve(root, settings["game"])),
               "-BuildDirectory", str(resolve(root, settings["build"])),
               "-OutputDirectory", str(resolve(root, settings["output"])),
               "-UserDirectory", str(resolve(root, settings["user"])),
               "-WindowResolution", settings["window"], "-PresentMode", settings["present"],
               "-FrameGeneration", settings["engine"], "-FpsPatch", settings["fps_patch"],
               "-FpsLimit", str(int(settings["fps_limit"])), "-Timeout", "0"]
    if settings["use_patch"]:
        command += ["-PatchDirectory", str(resolve(root, settings["patch"]))]
    else:
        command.append("-BaseOnly")
    if game_log_prefix is not None:
        command += ["-GameLogPrefix", str(game_log_prefix)]
    # Read ini at startup: Insert changes survive restarts without stale command overrides.
    return command


def launch_environment(settings: dict, stop: Path) -> dict[str, str]:
    environment = {k: v for k, v in os.environ.items() if not k.upper().startswith("BB_")}
    environment.update(BB_STOP_FILE=str(stop),
                       PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
    return environment


def import_save(source: Path, user: Path) -> tuple[Path, Path | None]:
    """Copy a decrypted SPRJ save; preserve its original and back up an earlier import."""
    source = source.resolve()
    if not source.is_dir() or not any(p.is_file() and (
            p.name.startswith("userdata") or p.suffix == ".dat") for p in source.iterdir()):
        raise ValueError("Select the decrypted SPRJ0005 folder containing userdata or .dat save files.")
    if source.name != "SPRJ0005":
        raise ValueError("Select SPRJ0005 itself, not the entire shadPS4 home folder.")
    title = source.parent.name if re.fullmatch(r"CUSA\d{5}", source.parent.name) else "CUSA00207"
    target = user.resolve() / "savedata/1" / title / "SPRJ0005"
    if target == source or target.is_relative_to(source) or source.is_relative_to(target):
        raise ValueError("The source and destination saves must be separate.")
    if any(p.is_symlink() or getattr(p.lstat(), "st_file_attributes", 0)
           & stat.FILE_ATTRIBUTE_REPARSE_POINT for p in (source, *source.rglob("*"))):
        raise ValueError("Save import does not follow linked files or folders.")
    target.parent.mkdir(parents=True, exist_ok=True)
    backup = None
    staging = Path(tempfile.mkdtemp(prefix=".save-import-", dir=target.parent))
    try:
        shutil.copytree(source, staging / "SPRJ0005")
        if target.exists():
            backup = target.with_name("SPRJ0005.backup-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
            target.rename(backup)
        try:
            (staging / "SPRJ0005").rename(target)
        except OSError:
            if backup is not None:
                backup.rename(target)
            raise
    finally:
        if staging.resolve().parent != target.parent.resolve() or not staging.name.startswith(".save-import-"):
            raise ValueError("Save staging cleanup escaped its intended destination.")
        shutil.rmtree(staging)
    return target, backup
