"""Fetch the pinned NVIDIA DLSS Super Resolution runtime and its SDK license.

The model stays in local build output; it is not a repository dependency.
An existing copy can be supplied with --source, but must match the official hash.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import urllib.request

VERSION = 'v310.9.1'
SHA256 = '3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983'
BASE = f'https://raw.githubusercontent.com/NVIDIA/DLSS/{VERSION}'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def fetch(destination, source=None):
    destination.mkdir(parents=True, exist_ok=True)
    model = destination / 'nvngx_dlss.dll'
    if model.exists() and digest(model) != SHA256:
        raise ValueError(f'{model} differs from the pinned model; choose another destination')
    if not model.exists():
        temporary = destination / 'nvngx_dlss.dll.part'
        try:
            if source:
                if digest(source) != SHA256:
                    raise ValueError('Source model does not match the pinned NVIDIA release')
                shutil.copyfile(source, temporary)
            else:
                with urllib.request.urlopen(BASE + '/lib/Windows_x86_64/rel/nvngx_dlss.dll', timeout=60) as response, temporary.open('wb') as stream:
                    shutil.copyfileobj(response, stream)
            if digest(temporary) != SHA256:
                raise ValueError('Downloaded model SHA-256 mismatch')
            temporary.replace(model)
        finally:
            temporary.unlink(missing_ok=True)
    with urllib.request.urlopen(BASE + '/LICENSE.txt', timeout=30) as response:
        (destination / 'LICENSE-DLSS-SDK.txt').write_bytes(response.read())
    print(f'DLSS {VERSION}: verified {model.resolve()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'out/windows')
    parser.add_argument('--source', type=Path, help='Reuse a matching local model instead of downloading it')
    args = parser.parse_args()
    fetch(args.destination, args.source)
