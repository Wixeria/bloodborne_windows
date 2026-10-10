"""Exercise modded /app0 and /hostapp through actual PS4 guest file imports."""
from paths import ROOT
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from test_loader import Image, nid
from windows_mods import build_overlay


def image_for(expected):
    image = Image()
    for index, (path, contents) in enumerate([
            ('/app0/dvdroot_ps4/chr/a.dcx', expected),
            ('/hostapp/dvdroot_ps4/chr/a.dcx', expected),
            ('/app0/dvdroot_ps4/chr/b.dcx', b'BASE')]):
        address = 0x1500 + index * 128
        data = path.encode() + b'\0'
        image.data[address:address+len(data)] = data
        image.rip(bytes.fromhex('48 8d 3d'), address)  # path -> rdi
        image.code += bytes.fromhex('31 f6 31 d2')  # flags = mode = 0
        image.call(nid('sceKernelOpen'))
        image.check(bytes.fromhex('83 f8 03'))  # first free descriptor
        image.code += bytes.fromhex('41 89 c4 44 89 e7')  # fd -> r12d -> edi
        image.rip(bytes.fromhex('48 8d 35'), 0x1900)  # buffer -> rsi
        image.code += bytes.fromhex('ba 04 00 00 00')
        image.call(nid('sceKernelRead'))
        image.check(bytes.fromhex('48 83 f8 04'))
        image.rip(bytes.fromhex('48 8d 05'), 0x1900)
        image.check(bytes.fromhex('81 38') + contents)
        image.code += bytes.fromhex('44 89 e7')
        image.call(nid('sceKernelClose'))
        image.check(bytes.fromhex('85 c0'))
    # Guests cannot mutate a hard-linked mod/game source through either mount.
    image.rip(bytes.fromhex('48 8d 3d'), 0x1500)
    image.code += bytes.fromhex('be 01 00 00 00 31 d2')
    image.call(nid('sceKernelOpen'))
    image.check(bytes.fromhex('3d 1e 00 02 80'))
    image.exit()
    return image.serialize()


def main():
    probe = Path(sys.argv[1]).resolve(strict=True)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('BB_')}
    with tempfile.TemporaryDirectory(prefix='bb-mod-runtime-') as directory:
        root = Path(directory)
        game = root/'game'
        (game/'dvdroot_ps4/chr').mkdir(parents=True)
        (game/'dvdroot_ps4/chr/a.dcx').write_bytes(b'GAME')
        (game/'dvdroot_ps4/chr/b.dcx').write_bytes(b'BASE')
        layers = []
        for name in ['ONE!', 'TWO!']:
            folder = root/name
            (folder/'chr').mkdir(parents=True)
            (folder/'chr/a.dcx').write_bytes(name.encode())
            layers.append((name,folder))
        for active, expected in [(layers,b'TWO!'),(layers[:1],b'ONE!'),([],b'GAME')]:
            mount = build_overlay(game,root/'mounts',active)
            boot = root/'boot.bin'
            boot.write_bytes(image_for(expected))
            result = subprocess.run([str(probe),str(boot),'--cpu-only','--app0',str(mount),
                '--user',str(root/'user'),'--timeout','5'],capture_output=True,env=env,timeout=12)
            if result.returncode:
                raise AssertionError(result.stdout.decode('utf-8',errors='replace') + result.stderr.decode('utf-8',errors='replace'))
        assert (game/'dvdroot_ps4/chr/a.dcx').read_bytes() == b'GAME'
        for name,folder in layers:
            assert (folder/'chr/a.dcx').read_bytes() == name.encode()
    print('Guest mod mount passed: priority, toggle-off fallback, app0/hostapp reads and read-only sources.')


if __name__ == '__main__': main()
