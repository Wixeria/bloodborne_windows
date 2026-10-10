"""Build/run the isolated Vulkan DLSS Frame Generation fixture (not the game).

Downloads stay in out/. The optional SM86 proxy is pinned and enabled only in
its own test directory. No game files, saves, bbport.ini or driver settings are
modified. A small visible window must stay focused during presentation tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SDK_VERSION = 'v2.14.1'
SDK_SHA = '92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b'
MOD_COMMIT = '329b4c85927c64a8dba2e25125e6d02a42ab63fe'
MOD_FILES = {
    'version.dll': '7489a89bf593cda243d95c62e6d8b75905d9774e27b7b2d53b91625a79c05ca4',
    'config/vulkan-sm86/dlssg_sm86.ini': 'e006644a7d370f5f8dfb039bfa7b454e4dfc0ef661d6d00cfa89648c93872dad',
    'config/vulkan-sm86/ReShade.ini': 'd979253377f6a0f691b926acda7730e01f35b2030a99e57786dbded4c47075f8',
    'THIRD_PARTY_NOTICES.txt': '8559e5b6549f9d03685675883dc423e8d768a79ec32d0bdda48f96005336a620',
}
RUNTIME_SHA = 'ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82'
SDK_DLLS = ('sl.interposer.dll', 'sl.common.dll', 'sl.dlss_g.dll',
            'sl.reflex.dll', 'sl.pcl.dll', 'nvngx_dlssg.dll', 'NvLowLatencyVk.dll')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def fetch(url, path, expected):
    if path.exists():
        if digest(path) != expected:
            raise ValueError(f'Hash mismatch: {path}; refusing overwrite')
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.part')
    try:
        with urllib.request.urlopen(url, timeout=60) as source, temporary.open('wb') as target:
            shutil.copyfileobj(source, target)
        if digest(temporary) != expected:
            raise ValueError(f'Download hash mismatch: {url}')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def prepare_sdk(cache):
    name = f'streamline-sdk-{SDK_VERSION}.zip'
    archive = cache / name
    fetch(f'https://github.com/NVIDIA-RTX/Streamline/releases/download/{SDK_VERSION}/{name}',
          archive, SDK_SHA)
    sdk = cache / 'sdk'
    if not (sdk / '.bbport-verified-sdk').exists():
        sdk.mkdir(exist_ok=True)
        with zipfile.ZipFile(archive) as z:
            # Only headers, runtime binaries and licenses needed by this fixture.
            for member in z.infolist():
                if member.filename.startswith(('include/', 'bin/x64/')) or member.filename == 'license.txt':
                    destination = (sdk / member.filename).resolve()
                    if not destination.is_relative_to(sdk.resolve()):
                        raise ValueError('SDK archive path escapes destination')
                    z.extract(member, sdk)
        (sdk / '.bbport-verified-sdk').write_text(SDK_SHA, encoding='utf-8')
    if digest(sdk / 'bin/x64/nvngx_dlssg.dll') != RUNTIME_SHA:
        raise ValueError('SDK Frame Generation runtime does not match the SM86 Vulkan build')
    return sdk


def verify_nvidia_signatures(directory):
    # Structured environment input avoids shell interpolation of paths.
    import os
    env = dict(os.environ, BB_DLSSG_SIGNATURE_DIR=str(directory))
    script = """
$ErrorActionPreference = 'Stop'
foreach ($name in @('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll','NvLowLatencyVk.dll')) {
    $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $env:BB_DLSSG_SIGNATURE_DIR $name)
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=NVIDIA Corporation') {
        throw "Invalid NVIDIA signature: $name ($($signature.Status))"
    }
}
"""
    subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script],
                   env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('official', 'sm86', 'sm86-off'), default='official')
    parser.add_argument('--capability-only', action='store_true')
    parser.add_argument('--cache', type=Path, default=ROOT / 'out/dlssg-probe-01')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/windows-dlssg-probe')
    parser.add_argument('--toolchain', type=Path,
                        default=ROOT / 'out/toolchains/llvm-mingw-20260922-ucrt-x86_64')
    parser.add_argument('--sdl', type=Path,
                        default=ROOT / 'out/toolchains/SDL3-3.4.18/x86_64-w64-mingw32')
    parser.add_argument('--vulkan-headers', type=Path, default=ROOT / 'out/toolchains/Vulkan-Headers')
    args = parser.parse_args()
    args.cache = args.cache.resolve()
    args.output = args.output.resolve()
    args.toolchain = args.toolchain.resolve()
    args.sdl = args.sdl.resolve()
    args.vulkan_headers = args.vulkan_headers.resolve()
    # Never install the mod into the ordinary game EXE directory.
    if not args.output.is_relative_to((ROOT / 'out').resolve()):
        raise ValueError('The probe output must be inside this workspace out/ directory')
    base = args.output / args.mode
    if base.exists() and any(base.iterdir()) and not (base / '.bbport-dlssg-probe').exists():
        raise ValueError(f'{base} is not a managed probe directory; refusing overwrite')
    base.mkdir(parents=True, exist_ok=True)
    (base / '.bbport-dlssg-probe').write_text('isolated test only\n', encoding='utf-8')
    sdk = prepare_sdk(args.cache)
    for name in SDK_DLLS:
        shutil.copy2(sdk / 'bin/x64' / name, base / name)
    verify_nvidia_signatures(base)
    shutil.copy2(sdk / 'license.txt', base / 'LICENSE-Streamline.txt')
    for license_file in (sdk / 'bin/x64').glob('*.license.txt'):
        shutil.copy2(license_file, base / license_file.name)
    for name in ('libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll'):
        shutil.copy2(args.toolchain / 'bin' / name, base / name)
    shutil.copy2(args.sdl / 'bin/SDL3.dll', base / 'SDL3.dll')
    if args.mode != 'official':
        mod = args.cache / 'mod'
        for name, expected in MOD_FILES.items():
            fetch(f'https://raw.githubusercontent.com/SilyNoMeta/dlssg_for_sm86/{MOD_COMMIT}/{name}',
                  mod / name, expected)
        shutil.copy2(mod / 'version.dll', base / 'version.dll')
        shutil.copy2(mod / 'THIRD_PARTY_NOTICES.txt', base / 'THIRD_PARTY_NOTICES-SM86.txt')
        shutil.copy2(mod / 'config/vulkan-sm86/dlssg_sm86.ini', base / 'dlssg_sm86.ini')
        # The fixture has no UI: avoid requesting UI recomposition without a UI buffer.
        (base / 'ReShade.ini').write_text(
            (mod / 'config/vulkan-sm86/ReShade.ini').read_text().replace('UIRecomposition=1', 'UIRecomposition=0'),
            encoding='utf-8')
    exe = base / 'dlssg-vulkan-probe.exe'
    build = [str(args.toolchain / 'bin/clang++.exe'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
             '-Wno-missing-field-initializers', '-Wno-unknown-pragmas',
             '-I' + str(sdk / 'include'), '-I' + str(args.sdl / 'include'),
             '-I' + str(args.vulkan_headers / 'include'), str(ROOT / 'tests/dlssg_vulkan_probe.cpp'),
             '-L' + str(args.sdl / 'lib'), '-lSDL3', '-o', str(exe)]
    with (base / 'build.log').open('w', encoding='utf-8') as log:
        subprocess.run(build, stdout=log, stderr=subprocess.STDOUT, check=True)
    command = [str(exe), str(base)]
    if args.mode != 'official':
        command.append('--mod')
    if not args.capability_only:
        command.append('--present')
    if args.mode == 'sm86-off':
        command.append('--fg-off')
    (base / 'logs').mkdir(exist_ok=True)
    print(f'Running {args.mode}; keep the test window focused. Log: {base / "probe.log"}', flush=True)
    timed_out = False
    with (base / 'probe.log').open('w', encoding='utf-8') as log:
        try:
            run = subprocess.run(command, cwd=base, stdout=log, stderr=subprocess.STDOUT, timeout=60)
            code = run.returncode
        except subprocess.TimeoutExpired:
            timed_out = True
            code = 124
    text = (base / 'probe.log').read_text(encoding='utf-8', errors='replace')
    match = re.search(r'PRESENT_RESULT: fg=(\w+) submitted=(\d+) presented=(\d+) steady-submitted=(\d+) steady-presented=(\d+) status=(\d+)', text)
    summary = {
        'mode': args.mode, 'exit_code': code, 'timed_out': timed_out,
        'capability_only': args.capability_only, 'sdk_package': SDK_VERSION,
        'sdk_archive_sha256': SDK_SHA, 'sm86_commit': MOD_COMMIT if args.mode != 'official' else None,
        'exe_sha256': digest(exe),
        'deployed_sha256': {name: digest(base / name) for name in SDK_DLLS},
        'note': 'Streamline may use NVIDIA App driver overrides; actual loaded plugins are recorded in logs/sl.log. This is not a game FPS benchmark.',
    }
    if not args.capability_only and 'FAIL: probe lost focus' in text:
        summary['invalid_reason'] = 'window_lost_focus'
    elif 'focus the probe window' in text:
        summary['invalid_reason'] = 'window_never_focused'
    elif timed_out:
        summary['invalid_reason'] = 'process_timeout'
    if match:
        summary['presentation'] = dict(zip(('fg', 'submitted', 'presented', 'steady_submitted', 'steady_presented', 'status'),
                                          (match[1], *(int(v) for v in match.groups()[1:]))))
    (base / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    for line in text.splitlines():
        if line.startswith(('GPU:', 'CAPABILITY_RESULT:', 'PRESENT_RESULT:', 'FAIL:')):
            print(line)
    print(f'Exit {code}; details: {base / "summary.json"}')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
