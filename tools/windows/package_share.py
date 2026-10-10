"""Create a clean Windows runtime + source ZIP from an explicit inclusion list.

No game dumps, linked game images, saves, private settings, mods, caches or Git
metadata are copied. Existing destinations are never overwritten.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PYTHON_URL = 'https://www.python.org/ftp/python/3.11.9/python-3.11.9-embed-amd64.zip'
PYTHON_SHA256 = '009d6bf7e3b2ddca3d784fa09f90fe54336d5b60f0e0f305c37f400bf83cfd3b'
SOURCE_DIRS = ('assets', 'docs', 'gpu', 'launcher', 'packaging', 'patches', 'scripts',
               'src', 'tests', 'third_party', 'tools')
SOURCE_ROOT_FILES = ('LICENSE', 'README.md', 'CMakeLists.txt', 'build-windows.ps1',
                     'run-windows.ps1', 'launcher-windows.cmd', 'build.sh', '.gitignore',
                     '.gitattributes', '.gitmodules')
SKIP_DIRS = {'.git', '.github', '__pycache__', '.pytest_cache', 'target', 'node_modules'}
SKIP_SUFFIXES = {'.log', '.obj', '.o', '.pdb', '.pyc', '.bak', '.tmp'}
RUNTIME_FILES = ('bb-game.exe', 'bb-probe.exe', 'SDL3.dll', 'libc++.dll', 'libunwind.dll',
    'libwinpthread-1.dll', 'avcodec-62.dll', 'avformat-62.dll', 'avutil-60.dll',
    'swresample-6.dll', 'swscale-9.dll', 'nvngx_dlss.dll', 'sl.interposer.dll',
    'sl.common.dll', 'sl.dlss_g.dll', 'sl.reflex.dll', 'sl.pcl.dll', 'nvngx_dlssg.dll',
    'NvLowLatencyVk.dll')
SM86_FILES = ('version.dll', 'dlssg_sm86.ini', 'ReShade.ini', 'THIRD_PARTY_NOTICES-SM86.txt')
CRT_FILES = ('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll',
             'msvcp140_codecvt_ids.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def source_paths(root):
    for name in SOURCE_ROOT_FILES:
        path = root / name
        if path.is_file():
            yield path
    for name in SOURCE_DIRS:
        for base, dirs, files in os.walk(root / name, followlinks=False):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS
                             and not (Path(base) / d).is_symlink())
            for name in sorted(files):
                path = Path(base) / name
                if name == '.git' or path.is_symlink() or path.suffix.lower() in SKIP_SUFFIXES:
                    continue
                yield path

def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)

def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8', newline='\n')

def build(destination, python_archive, crt, runtime):
    destination = destination.resolve()
    zip_path = Path(str(destination) + '.zip')
    if destination.exists() or zip_path.exists():
        raise ValueError('Choose a new destination; existing packages are not overwritten.')
    destination.mkdir(parents=True)
    package = destination / 'Bloodborne-PC-Windows'
    package.mkdir()
    for path in source_paths(ROOT):
        relative = path.relative_to(ROOT)
        target = package / relative
        copy(path, target)
        if path.suffix.lower() == '.md':
            text = target.read_text(encoding='utf-8')
            write(target, text.replace(Path.home().name, 'USER'))
    for name in RUNTIME_FILES:
        copy(runtime / name, package / 'out/windows' / name)
    for name in SM86_FILES:
        copy(runtime / name, package / 'optional/sm86' / name)
    copy(ROOT / 'out/windows-launcher/BloodborneLauncher.exe',
         package / 'out/windows-launcher/BloodborneLauncher.exe')
    for name in CRT_FILES:
        copy(crt / name, package / 'out/windows' / name)
    copy(crt / 'vcruntime140.dll', package / 'out/windows-launcher/vcruntime140.dll')
    if not python_archive.exists():
        python_archive.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(PYTHON_URL, timeout=45) as response, python_archive.open('wb') as target:
            shutil.copyfileobj(response, target)
    if digest(python_archive) != PYTHON_SHA256:
        raise ValueError('Embedded Python archive differs from the official pinned SHA-256.')
    py = package / 'runtime/python'
    py.mkdir(parents=True)
    with zipfile.ZipFile(python_archive) as archive:
        for name in archive.namelist():
            if Path(name).name != name or ':' in name:
                raise ValueError('Unexpected embedded Python archive layout.')
        archive.extractall(py)
    write(py / 'python311._pth', 'python311.zip\n.\n../..\n../../scripts\n../../tests\n../../tools/windows\n')
    notices = package / 'licenses'
    licenses = {
        'SDL3.txt': ROOT / 'out/toolchains/SDL3-3.4.18/LICENSE.txt',
        'FFmpeg-LGPL.txt': ROOT / 'out/toolchains/ffmpeg-n8.1.3-14-g330caae0c1-win64-lgpl-shared-8.1/LICENSE.txt',
        'LLVM.txt': ROOT / 'out/toolchains/llvm-mingw-20260922-ucrt-x86_64/LICENSE.TXT',
        'Winpthreads.txt': ROOT / 'out/toolchains/llvm-mingw-20260922-ucrt-x86_64/x86_64-w64-mingw32/share/mingw32/COPYING.winpthreads.txt',
        'MinGW-runtime.txt': ROOT / 'out/toolchains/llvm-mingw-20260922-ucrt-x86_64/x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt',
        'DLSS-SDK.txt': ROOT / 'out/windows/LICENSE-DLSS-SDK.txt',
    }
    for name, source in licenses.items():
        copy(source, notices / name)
    for path in runtime.iterdir():
        if path.name == 'LICENSE-Streamline.txt' or path.name.endswith('.license.txt'):
            copy(path, package / 'out/windows' / path.name)
    vs_root = next((p for p in crt.parents if (p / 'Licenses/1033/Redist.txt').is_file()), None)
    if vs_root:
        copy(vs_root / 'Licenses/1033/Redist.txt', notices / 'Microsoft-Redist.txt')
    cargo = subprocess.run(['cargo.exe', 'metadata', '--manifest-path',
        str(ROOT / 'launcher/rust/Cargo.toml'), '--locked', '--offline',
        '--filter-platform', 'x86_64-pc-windows-msvc', '--format-version', '1'],
        capture_output=True, encoding='utf-8', check=True)
    metadata = json.loads(cargo.stdout)
    resolved = {node['id'] for node in metadata['resolve']['nodes']}
    rust_index = []
    for crate in metadata['packages']:
        if crate['id'] not in resolved or not crate.get('source'):
            continue
        base = Path(crate['manifest_path']).parent
        folder = notices / 'Rust' / (crate['name'] + '-' + crate['version'])
        paths = [p for p in base.iterdir() if p.is_file() and p.name.upper().startswith(
            ('LICENSE', 'LICENCE', 'COPYING', 'NOTICE', 'UNLICENSE', 'COPYRIGHT'))]
        if crate.get('license_file'):
            extra = base / crate['license_file']
            if extra.is_file() and extra not in paths:
                paths.append(extra)
        if not paths:
            paths = [p for p in base.iterdir() if p.is_file() and p.name.upper().startswith('README')]
        for path in paths:
            copy(path, folder / path.name)
        rust_index.append({k: crate[k] for k in ('name', 'version', 'license', 'authors', 'repository', 'source')})
    write(notices / 'Rust/index.json', json.dumps(rust_index, indent=2) + '\n')
    write(package / 'out/windows-launcher/settings.json', json.dumps(dict(
        game='', patch='', use_patch=False, mods_enabled=True, mods=[],
        user='user', output='out/windows-game', build='out/windows', engine='off',
        window='1920x1080', present='Immediate', fps_patch='uncap', fps_limit=0,
        launch_options='', fullscreen=False), indent=2) + '\n')
    write(package / 'enable-sm86.cmd', '@echo off\ncd /d "%~dp0"\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "tools\\windows\\enable_portable_sm86.ps1"\npause\n')
    write(package / 'START_HERE.md', """# Bloodborne PC — Windows portable package

## Play / Oyna

1. Extract the entire ZIP to a writable folder; do not run inside the ZIP.
2. Open `launcher-windows.cmd`.
3. In Play → Game files, select your own decrypted CUSA03173 v1.09 game.
   A separate matching update folder is also supported. No game files are included.
4. Save settings, then Launch game. First preparation can take a while.

ZIP'i tamamen bir klasöre çıkar, `launcher-windows.cmd` aç, kendi CUSA03173
01.09 oyun klasörünü seç, ayarları kaydet ve Launch game'e bas.

The launcher is Rust/GPUI. Bundled Python only prepares the game image; Python,
Rust and C++ development tools are not needed to play. Windows x64, Vulkan-capable
hardware and a compatible installed GPU driver are required. Saves are created
under `user`; settings/logs/generated game images under `out/windows-game`.
Insert opens settings; F11 toggles fullscreen; F8 releases the mouse.

## DLSS, Reflex and frame generation

DLSS SR and the NVIDIA Reflex/official FG runtimes are included. In Graphics,
select DLSS and a quality preset. Official FG uses Play → Runtime → Official
NVIDIA FG, with Runtime folder `out/windows`; enable FG in Graphics or Insert.
Availability and multiplier limits depend on GPU/runtime support. FG is off by
default. Optional FSR 4 model assets are not included; see the source docs.

For experimental RTX 20/30 SM86 FG: close the launcher and run `enable-sm86.cmd`
once, then reopen it. This creates a separate managed runtime and selects it.
Common DLLs are copied locally instead of stored twice in this ZIP. Choose DLSS
and enable FG in Graphics or Insert. To return to standard/official mode, change
Runtime folder to `out/windows` and select Standard Windows or Official NVIDIA FG.

Vertex explosion workaround: Game Effects → Vertex explosion fix → Disable
FaceGen. Save and restart. It disables custom face shapes while enabled.

## Develop / Geliştir

The root contains the current source snapshot, tests, Cargo.lock, CMake/build
scripts and vendored submodule sources. There is no Git history or build cache.
Internet access and the documented developer toolchains are needed to rebuild.
See `docs/WINDOWS_PORT.md` and `docs/WINDOWS_LAUNCHER.md`.

Runtime rebuild: `powershell -ExecutionPolicy Bypass -File build-windows.ps1 -Bootstrap -Gpu`
Launcher rebuild: `powershell -ExecutionPolicy Bypass -File tools/windows/build_launcher.ps1 -Test`
Streamline-enabled rebuilds also require SDK headers as described in
`docs/WINDOWS_FRAME_GENERATION.md`. A normal rebuild uses the included submodule
sources even though this package has no `.git` directory.

Game dumps, linked game images, personal settings/saves/mods, test logs, screenshots,
debug symbols, toolchains and old executable backups are excluded. Third-party
notices are retained in `licenses`, the runtime folders and vendored source trees.
`package-manifest.json` records files and hashes; `SHA256SUMS.txt` verifies contents.
""")
    write(notices / 'DEPENDENCIES.md', """# Runtime and source provenance

Project: GPL-2.0-or-later; see root LICENSE and included source snapshot.
Upstream game translation: https://github.com/deadinside28/bloodborne_pc/tree/0.5
shadPS4 portions and vendored libraries retain their source license notices.
Rust launcher dependencies: launcher/rust/Cargo.lock (GPUI Kit 0.7.1).
LLVM/MinGW runtime: 20260922, https://github.com/mstorsjo/llvm-mingw
SDL: 3.4.18, https://github.com/libsdl-org/SDL
FFmpeg: n8.1.3-14-g330caae0c1, LGPL shared Windows build by BtbN.
Build/source: https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-10-03-18-14
FFmpeg source: https://github.com/FFmpeg/FFmpeg/tree/330caae0c1
The FFmpeg DLLs are dynamically linked and replaceable.
Microsoft CRT: VS 18 release x64 redistributable; see Microsoft-Redist.txt.
Python: official embedded CPython 3.11.9; runtime/python/LICENSE.txt.
https://www.python.org/downloads/release/python-3119/
DLSS SR SDK: https://github.com/NVIDIA/DLSS/tree/v310.9.1
Streamline: https://github.com/NVIDIA-RTX/Streamline/tree/v2.14.1
NVIDIA runtimes retain the included NVIDIA SDK/license terms.
SM86 proxy: https://github.com/SilyNoMeta/dlssg_for_sm86/tree/329b4c85927c64a8dba2e25125e6d02a42ab63fe
See optional/sm86/THIRD_PARTY_NOTICES-SM86.txt; this experiment is not official
NVIDIA RTX 20/30 frame-generation support.
""")
    files = sorted(p for p in package.rglob('*') if p.is_file())
    manifest = dict(format=1, upstream='0.5', kind='windows-runtime-and-source',
        python_archive_sha256=PYTHON_SHA256, personal_data_included=False,
        game_data_included=False, files={p.relative_to(package).as_posix(): dict(
            bytes=p.stat().st_size, sha256=digest(p)) for p in files})
    write(package / 'package-manifest.json', json.dumps(manifest, indent=2) + '\n')
    files = sorted(p for p in package.rglob('*') if p.is_file())
    write(package / 'SHA256SUMS.txt', ''.join(f'{digest(p)}  {p.relative_to(package).as_posix()}\n' for p in files))
    temporary_zip = Path(str(zip_path) + '.part')
    with zipfile.ZipFile(temporary_zip, 'x', compression=zipfile.ZIP_DEFLATED,
                         compresslevel=6, strict_timestamps=False) as archive:
        for path in sorted(p for p in package.rglob('*') if p.is_file()):
            archive.write(path, path.relative_to(destination).as_posix())
    temporary_zip.replace(zip_path)
    print(json.dumps(dict(zip=str(zip_path), sha256=digest(zip_path),
                         files=len(files)+1, zip_bytes=zip_path.stat().st_size,
                         unpacked_bytes=sum(p.stat().st_size for p in package.rglob('*') if p.is_file())), indent=2))
    return zip_path

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--destination', type=Path, required=True)
    p.add_argument('--runtime', type=Path, default=ROOT / 'out/windows-fg-game')
    p.add_argument('--python-archive', type=Path, default=ROOT / 'out/portable-cache/python-3.11.9-embed-amd64.zip')
    p.add_argument('--crt', type=Path, required=True, help='Visual Studio release x64 CRT redistributable directory')
    args = p.parse_args()
    build(args.destination, args.python_archive, args.crt, args.runtime)

if __name__ == '__main__':
    main()
