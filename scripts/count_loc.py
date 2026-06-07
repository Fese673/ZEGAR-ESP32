#!/usr/bin/env python3
"""Count code lines in include/ and src/ directories, excluding markdown files."""

import os
import pathlib

EXCLUDE_EXTENSIONS = {'.md'}
TARGET_DIRS = ['include', 'src']


def count_lines(root_path: pathlib.Path) -> int:
    total_lines = 0
    for target in TARGET_DIRS:
        target_path = root_path / target
        if not target_path.exists():
            continue
        for path in target_path.rglob('*'):
            if path.is_file():
                if path.suffix.lower() in EXCLUDE_EXTENSIONS:
                    continue
                try:
                    with path.open('r', encoding='utf-8', errors='ignore') as f:
                        lines = f.readlines()
                except OSError:
                    continue
                total_lines += len(lines)
    return total_lines


if __name__ == '__main__':
    root = pathlib.Path(__file__).resolve().parent.parent
    lines = count_lines(root)
    print(f'Linie kodu w include/ i src/ (bez .md): {lines}')
