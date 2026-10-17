#!/usr/bin/env python3
"""Refresh Bot Status code in its installed folder, preserving saved variables."""
import argparse
import hashlib
from pathlib import Path
import shutil


def update(target: Path, source: Path = None):
    source = (source or Path(__file__).resolve().parents[1] / 'addons/WoweeBotStatus').resolve()
    target = target.resolve()
    if target.name != 'WoweeBotStatus' or target == source or source in target.parents:
        raise ValueError('Target must be an installed WoweeBotStatus folder, separate from the source folder')
    toc = source / 'WoweeBotStatus.toc'
    if not toc.is_file():
        raise ValueError('Bot Status source is incomplete')
    names = {'WoweeBotStatus.toc'}
    for line in toc.read_text(encoding='utf-8-sig').splitlines():
        line = line.strip()
        if line and not line.startswith('#'):
            names.add(line)
    files = sorted((source / name).resolve() for name in names)
    if any(p.parent != source or not p.is_file() or p.suffix.lower() not in {'.lua', '.toc', '.xml'} for p in files):
        raise ValueError('Bot Status manifest refers to a missing or unsupported code file')
    target.mkdir(parents=True, exist_ok=True)
    for path in files:
        destination = target / path.name
        shutil.copy2(path, destination)
        if hashlib.sha256(destination.read_bytes()).digest() != hashlib.sha256(path.read_bytes()).digest():
            raise RuntimeError('Copy verification failed: ' + path.name)
    return [p.name for p in files]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, type=Path,
                        help='Installed addon directory, e.g. build/bin/addons/WoweeBotStatus')
    args = parser.parse_args()
    try:
        names = update(args.target)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, str(error) + '\n')
    print('Updated ' + str(args.target.resolve()) + ': ' + ', '.join(names))
    print('Saved variables and other addons were preserved. Restart WoWee or reload its interface.')
