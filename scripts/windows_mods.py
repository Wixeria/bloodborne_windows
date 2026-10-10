"""Build a cached loose-file mod mount without changing the game or mod folders.

Windows uses ordinary directories and hard links, without Developer Mode/admin.
Mod files on another drive are copied; the large game dump must share the mount drive.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from game_overlay import inventory
from mods import GAME_FOLDERS, child


def linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def layout(folder, depth=0):
    folder = Path(folder)
    if not folder.is_dir() or linked(folder) or depth > 5:
        raise ValueError(f'Expected an extracted mod folder: {folder}')
    dvd = child(folder, 'dvdroot_ps4')
    if dvd.is_dir():
        if linked(dvd):
            raise ValueError(f'Mod links are unsupported: {dvd}')
        return dvd, Path('dvdroot_ps4'), False
    if folder.name.casefold() == 'dvdroot_ps4':
        return folder, Path('dvdroot_ps4'), False
    folders = sorted((p for p in folder.iterdir() if p.is_dir() and not p.name.startswith('.')),
                     key=lambda p: p.name.casefold())
    if any(p.name.casefold() in GAME_FOLDERS for p in folders):
        return folder, Path('dvdroot_ps4'), True
    if folder.name.casefold() in GAME_FOLDERS:
        return folder, Path('dvdroot_ps4') / folder.name.lower(), False
    if len(folders) == 1:
        return layout(folders[0], depth + 1)
    raise ValueError(f'{folder}: expected dvdroot_ps4 or asset folders such as chr, map, param')


def assets(folder):
    root, prefix, bare = layout(folder)
    entries = inventory(root)
    result = {}
    for _, (relative, source, directory, size, modified) in entries.items():
        if bare and Path(relative).parts[0].casefold() not in GAME_FOLDERS:
            continue
        relative = (prefix / relative).as_posix()
        key = relative.casefold()
        result[key] = (relative, source, directory, size, modified)
    if not any(not entry[2] for entry in result.values()):
        raise ValueError(f'No loose game files found in {folder}')
    return result


def layers_from_config(config):
    settings = json.loads(Path(config).read_text(encoding='utf-8-sig'))
    if type(settings.get('enabled')) is not bool or not isinstance(settings.get('mods'), list):
        raise ValueError('Mod profile requires enabled and a mods list')
    layers = []
    for entry in settings['mods']:
        if (not isinstance(entry, dict) or type(entry.get('enabled')) is not bool or
                not isinstance(entry.get('path'), str) or not isinstance(entry.get('name'), str)):
            raise ValueError('Each mod requires a name, folder path and enabled boolean')
        if settings['enabled'] and entry['enabled']:
            source = Path(entry['path'])
            if not source.is_absolute():
                raise ValueError('Mod profile paths must be absolute')
            layers.append((entry['name'], source))
    return layers


def digest(path):
    with Path(path).open('rb') as handle:
        checksum = hashlib.sha256()
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            checksum.update(chunk)
        return checksum.hexdigest()


def build_overlay(game, output, layers):
    game = Path(game).resolve(strict=True)
    if not layers:
        return game
    output = Path(output).resolve()
    for _, folder in layers:
        if linked(Path(folder)):
            raise ValueError(f'Mod links are unsupported: {folder}')
    sources = [Path(folder).resolve(strict=True) for _, folder in layers]
    for source in [game, *sources]:
        if output.is_relative_to(source) or source.is_relative_to(output):
            raise ValueError('Mod mount output must be separate from the game and mod folders')
    merged = inventory(game)
    owners, conflicts, mod_file_count = {}, [], 0
    for (name, _), folder in zip(layers, sources):
        entries = assets(folder)
        files = sum(not e[2] for e in entries.values())
        mod_file_count += files
        print(f'Mods: {name}: {files} loose files', file=sys.stderr)
        for key, entry in entries.items():
            if key in merged and merged[key][2] != entry[2]:
                raise ValueError(f'File/directory conflict: {entry[0]}')
            if not entry[2]:
                if key in owners:
                    conflicts.append(dict(path=entry[0], previous=owners[key], winner=name))
                    print(f'Mods: {entry[0]}: {owners[key]} -> {name}', file=sys.stderr)
                owners[key] = name
            if key in merged:
                entry = (merged[key][0], *entry[1:])
            merged[key] = entry
    # Single-folder mods may introduce dvdroot_ps4 or one of its directories.
    for key, entry in list(merged.items()):
        relative = Path(entry[0])
        for parent in relative.parents:
            if parent == Path('.'):
                continue
            merged.setdefault(parent.as_posix().casefold(),
                              (parent.as_posix(), None, True, 0, 0))
    entries = []
    for key, (relative, source, directory, size, modified) in sorted(merged.items()):
        # Resolve every parent through its canonical merged spelling.
        parts = relative.split('/')
        for index in range(1, len(parts)):
            parts[:index] = merged['/'.join(parts[:index]).casefold()][0].split('/')
        entries.append(('/'.join(parts), str(source) if source else None, directory, size, modified,
                        key in owners))
    fingerprint = hashlib.sha256(json.dumps([entries, [(n, str(p)) for (n, _), p in zip(layers, sources)]],
                                            ensure_ascii=True).encode()).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    manifest = output / f'mods-{fingerprint}.json'
    if manifest.exists():
        cached = json.loads(manifest.read_text(encoding='utf-8'))
        mount = Path(cached['mount'])
        if (not mount.is_absolute() or mount.parent != output or linked(mount) or
                mount.resolve().parent != output):
            raise ValueError('Invalid cached mod mount')
        copied = cached.get('copied', {})
        for relative, source, directory, _, _, _ in entries:
            destination = mount / relative
            if linked(destination) or (directory and not destination.is_dir()):
                raise ValueError('Incomplete cached mod mount; choose a new output folder')
            if not directory and (not destination.is_file() or
                    (digest(destination) != copied[relative] if relative in copied else
                     not os.path.samefile(destination, source))):
                raise ValueError('Incomplete cached mod mount; choose a new output folder')
        print(f'Mods: reusing {mount.name}; originals unchanged', file=sys.stderr)
        return mount
    mount = Path(tempfile.mkdtemp(prefix=f'mod-game-{fingerprint[:12]}-', dir=output))
    copied = {}
    try:
        for relative, source, directory, _, _, modded in sorted(entries,
                key=lambda e: (len(Path(e[0]).parts), e[0])):
            destination = mount / relative
            if directory:
                destination.mkdir(exist_ok=True)
            else:
                try:
                    os.link(source, destination)
                except OSError as error:
                    if not modded:
                        raise ValueError('Place the output folder on the same drive/filesystem as the game') from error
                    shutil.copy2(source, destination)
                    copied[relative] = digest(destination)
        report = dict(mount=str(mount), game=str(game), mods=[name for name, _ in layers],
                      files=mod_file_count, conflicts=conflicts, copied=copied)
        # The manifest appears only after the complete mount exists.
        with tempfile.NamedTemporaryFile('w', encoding='utf-8', dir=output, delete=False) as temp:
            json.dump(report, temp, indent=2)
            temp.write('\n')
        os.replace(temp.name, manifest)
    except BaseException:
        # This is exactly the newly created staging directory, never a source.
        if mount.resolve().parent != output or linked(mount):
            raise ValueError('Refusing to remove an unexpected mod staging path')
        shutil.rmtree(mount)
        raise
    print(f'Mods: {len(layers)} active; {len(conflicts)} overlaps; originals unchanged', file=sys.stderr)
    return mount


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--config', required=True, type=Path)
    args = parser.parse_args()
    try:
        print(build_overlay(args.game, args.out, layers_from_config(args.config)))
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(1, f'Mods: {error}\n')
