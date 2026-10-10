"""Merge a decrypted base game and official update into a read-only runtime mount.

Files are hard-linked into a generated directory, so Windows needs neither
privileged symlinks nor a second full game copy. Never edit the generated files:
they share data with the originals. The runtime mounts /app0 read-only.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

from prepare import sfo


def inventory(root):
    result = {}
    for directory, folders, files in os.walk(root, followlinks=False):
        for name in sorted(folders + files):
            source = Path(directory) / name
            if source.is_symlink() or getattr(source, 'is_junction', lambda: False)():
                raise ValueError(f'Game/update links are unsupported: {source}')
            relative = source.relative_to(root).as_posix()
            key = relative.casefold()
            if key in result:
                raise ValueError(f'Case-conflicting game paths: {relative}')
            stat = source.stat()
            result[key] = (relative, source, source.is_dir(), stat.st_size, stat.st_mtime_ns)
    return result


def build_overlay(base, patch, output):
    base, patch = Path(base).resolve(strict=True), Path(patch).resolve(strict=True)
    output = Path(output).resolve()
    if base == patch or base.is_relative_to(patch) or patch.is_relative_to(base):
        raise ValueError('Base and update must be separate directories')
    if output.is_relative_to(base) or output.is_relative_to(patch):
        raise ValueError('Overlay output must be outside the game and update')
    metadata = [sfo((p / 'sce_sys/param.sfo').read_bytes()) for p in (base, patch)]
    if not metadata[0].get('TITLE_ID') or metadata[0]['TITLE_ID'] != metadata[1].get('TITLE_ID'):
        raise ValueError('Base/update TITLE_ID mismatch')
    if metadata[1].get('CATEGORY') != 'gp' or not metadata[1].get('APP_VER'):
        raise ValueError('Expected an official update SFO with CATEGORY=gp and APP_VER')
    versions = [m.get('APP_VER', '') for m in metadata]
    if not all(isinstance(v, str) and re.fullmatch(r'\d{2}\.\d{2}', v) for v in versions):
        raise ValueError('Invalid base/update APP_VER')
    if tuple(map(int, versions[1].split('.'))) < tuple(map(int, versions[0].split('.'))):
        raise ValueError('Update version is older than the base game')
    if not all((p / 'eboot.bin').is_file() for p in (base, patch)):
        raise ValueError('Base and update must contain a decrypted eboot.bin')
    merged = inventory(base)
    for key, entry in inventory(patch).items():
        if key in merged and merged[key][2] != entry[2]:
            raise ValueError(f'File/directory conflict: {entry[0]}')
        # Preserve the base spelling of shared paths (case-sensitive hosts too).
        if key in merged:
            entry = (merged[key][0], *entry[1:])
        merged[key] = entry
    # Normalize parents to their merged spelling when update casing differs.
    entries = []
    for key, (relative, source, directory, size, modified) in sorted(merged.items()):
        parts = relative.split('/')
        for index in range(1, len(parts)):
            parent = merged['/'.join(parts[:index]).casefold()][0]
            parts[:index] = parent.split('/')
        entries.append(('/'.join(parts), str(source), directory, size, modified))
    fingerprint = hashlib.sha256(json.dumps(entries, ensure_ascii=True).encode()).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    manifest = output / f'update-{fingerprint}.json'
    if manifest.is_file():
        cached = json.loads(manifest.read_text(encoding='utf-8'))
        mount = Path(cached['mount']).resolve(strict=True)
        if mount.parent != output or mount.is_symlink() or getattr(mount, 'is_junction', lambda: False)():
            raise ValueError('Invalid cached overlay location')
        if not all((mount / e[0]).is_dir() if e[2] else
                   (mount / e[0]).is_file() and os.path.samefile(mount / e[0], e[1])
                   for e in entries):
            raise ValueError('Incomplete cached overlay; use a new output directory')
        return mount
    mount = Path(tempfile.mkdtemp(prefix=f'game-{metadata[1]["APP_VER"]}-{fingerprint[:12]}-', dir=output))
    for relative, source, directory, _, _ in sorted(entries, key=lambda e: (len(Path(e[0]).parts), e[0])):
        destination = mount / relative
        if directory:
            destination.mkdir(exist_ok=True)
        else:
            try:
                os.link(source, destination)
            except OSError as exc:
                raise ValueError('Hard-link overlay requires output on the same filesystem as both dumps') from exc
    manifest.write_text(json.dumps(dict(base=str(base), patch=str(patch), mount=str(mount),
                                       title_id=metadata[1]['TITLE_ID'], app_version=metadata[1]['APP_VER'],
                                       files=sum(not e[2] for e in entries)), indent=2) + '\n', encoding='utf-8')
    print(f'Update overlay: {metadata[1]["TITLE_ID"]} {metadata[1]["APP_VER"]}, '
          f'{sum(not e[2] for e in entries)} files; originals unchanged', file=sys.stderr)
    return mount


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('patch', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(build_overlay(args.base, args.patch, args.out))
    except (ValueError, OSError) as exc:
        parser.exit(1, f'Update overlay: {exc}\n')
