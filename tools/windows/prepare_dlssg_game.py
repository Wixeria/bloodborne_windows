"""Deploy the optional game FG experiment into a separate executable directory.

Build first with build-windows.ps1 -Gpu -StreamlineHeaders <SDK/include>.
Official NVIDIA DLL signatures and the experimental proxy hash are verified.
The ordinary build directory, game dump, saves and NVIDIA profiles stay intact.
"""
import argparse
import shutil
from pathlib import Path
from test_dlssg import (ROOT, SDK_DLLS, MOD_FILES, MOD_COMMIT, prepare_sdk,
                       verify_nvidia_signatures, fetch, digest)
import json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('official', 'sm86'), default='official')
    parser.add_argument('--build', type=Path, default=ROOT / 'out/windows')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/windows-fg-game')
    parser.add_argument('--cache', type=Path, default=ROOT / 'out/dlssg-probe-01')
    args = parser.parse_args()
    base, build = args.output.resolve(), args.build.resolve()
    if not base.is_relative_to((ROOT / 'out').resolve()) or base == build:
        raise ValueError('FG deployment must use a separate directory inside workspace out/')
    if base.exists() and any(base.iterdir()) and not (base / '.bbport-dlssg-game').exists():
        raise ValueError('Refusing to overwrite an unmanaged directory')
    exe = build / 'bb-game.exe'
    if not exe.is_file():
        raise ValueError('Build bb-game.exe first')
    if b'BB_STREAMLINE_DIR' not in exe.read_bytes():
        raise ValueError('Executable was built without Streamline headers; rebuild with -StreamlineHeaders')
    base.mkdir(parents=True, exist_ok=True)
    (base / '.bbport-dlssg-game').write_text('isolated experimental game runtime\n')
    shutil.copy2(exe, base / exe.name)
    # run-windows.ps1 also checks the CPU diagnostic executable during preparation.
    if (build / 'bb-probe.exe').is_file():
        shutil.copy2(build / 'bb-probe.exe', base / 'bb-probe.exe')
    for dll in build.glob('*.dll'):
        # Never inherit an unrelated injection DLL from a build directory.
        if dll.name.lower() not in ('version.dll', 'dxgi.dll', 'd3d12.dll', 'vulkan-1.dll'):
            shutil.copy2(dll, base / dll.name)
    sdk = prepare_sdk(args.cache.resolve())
    for name in SDK_DLLS:
        shutil.copy2(sdk / 'bin/x64' / name, base / name)
    verify_nvidia_signatures(base)
    shutil.copy2(sdk / 'license.txt', base / 'LICENSE-Streamline.txt')
    for license_file in (sdk / 'bin/x64').glob('*.license.txt'):
        shutil.copy2(license_file, base / license_file.name)
    if args.mode == 'sm86':
        mod = args.cache.resolve() / 'mod'
        for name, sha in MOD_FILES.items():
            fetch(f'https://raw.githubusercontent.com/SilyNoMeta/dlssg_for_sm86/{MOD_COMMIT}/{name}', mod/name, sha)
        for src, dest in [('version.dll','version.dll'),('THIRD_PARTY_NOTICES.txt','THIRD_PARTY_NOTICES-SM86.txt'),
                          ('config/vulkan-sm86/dlssg_sm86.ini','dlssg_sm86.ini')]:
            shutil.copy2(mod / src, base / dest)
        # A HUDless scene is tagged; separate UI alpha recomposition is still pending.
        (base / 'ReShade.ini').write_text((mod / 'config/vulkan-sm86/ReShade.ini').read_text()
                                        .replace('UIRecomposition=1','UIRecomposition=0'))
    else:
        # These are managed files in this verified private deployment only.
        for name in ('version.dll','dlssg_sm86.ini','ReShade.ini'):
            (base / name).unlink(missing_ok=True)
    (base / 'deployment.json').write_text(json.dumps({
        'mode': args.mode, 'exe_sha256': digest(base / exe.name),
        'deployed_sha256': {p.name:digest(p) for p in base.glob('*.dll')},
        'sm86_commit': MOD_COMMIT if args.mode == 'sm86' else None,
        'experimental': True}, indent=2)+'\n')
    print(f'Game FG runtime prepared: {base}')


if __name__ == '__main__':
    main()
