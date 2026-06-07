"""Sort #include directives at the top of C++ source/header files.

Convention:
  1. Own header first (for .cpp files — the matching .h)
  2. blank line
  3. System/platform headers (angle brackets, <...>)
  4. blank line
  5. Project headers (quoted, "..."), sorted alphabetically

Only touches the contiguous #include block at the top of the file.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

INCLUDE_RE = re.compile(r'^\s*#include\s+([<"].+?[>"])')
COMMENT_RE = re.compile(r'^\s*//')
BLANK_RE = re.compile(r'^\s*$')
PRAGMA_ONCE_RE = re.compile(r'^\s*#pragma\s+once')
IFNDEF_RE = re.compile(r'^\s*#ifndef\s+\w+')
DEFINE_RE = re.compile(r'^\s*#define\s+\w+')
ENDIF_RE = re.compile(r'^\s*#endif')


def classify_include(token: str) -> int:
    """0 = own header, 1 = system (<>), 2 = project ("")"""
    if token.startswith('<'):
        return 1
    return 2


def sort_includes_in_file(filepath: Path) -> bool:
    lines = filepath.read_text(encoding='utf-8').splitlines(keepends=True)
    if not lines:
        return False

    # Find the start of the include block
    start = 0
    for i, line in enumerate(lines):
        if INCLUDE_RE.match(line):
            start = i
            break
        # Skip past #pragma once, #ifndef guards, blank lines before includes
        if PRAGMA_ONCE_RE.match(line) or IFNDEF_RE.match(line) or DEFINE_RE.match(line) or COMMENT_RE.match(line) or BLANK_RE.match(line):
            continue
        # If we hit non-include, non-guard code before any include, bail
        if not BLANK_RE.match(line) and not line.strip().startswith('#'):
            return False

    # Collect contiguous include block
    end = start
    for i in range(start, len(lines)):
        line = lines[i]
        if INCLUDE_RE.match(line) or BLANK_RE.match(line) or COMMENT_RE.match(line):
            end = i + 1
        else:
            break

    # Extract includes
    includes = []
    for i in range(start, end):
        m = INCLUDE_RE.match(lines[i])
        if m:
            includes.append((classify_include(m.group(1)), m.group(1), lines[i].rstrip()))

    if not includes:
        return False

    # Determine own header
    if filepath.suffix == '.cpp':
        own_header = filepath.stem + '.h'
    else:
        own_header = None

    # Split into groups
    own = []
    system = []
    project = []

    for cls, token, raw in includes:
        if own_header and token == f'"{own_header}"':
            own.append(raw)
        elif cls == 1:
            system.append(raw)
        else:
            project.append(raw)

    # Sort system and project alphabetically by token
    system.sort(key=lambda x: INCLUDE_RE.match(x).group(1).lower())
    project.sort(key=lambda x: INCLUDE_RE.match(x).group(1).lower())

    # Build new include block
    new_lines = []
    if own:
        new_lines.extend(own)
        if system or project:
            new_lines.append('')
    if system:
        new_lines.extend(system)
        if project:
            new_lines.append('')
    if project:
        new_lines.extend(project)

    # Reconstruct file
    new_lines = [l + '\n' for l in new_lines]
    new_content = lines[:start] + new_lines + lines[end:]

    new_text = ''.join(new_content)
    old_text = ''.join(lines)
    if new_text != old_text:
        filepath.write_text(new_text, encoding='utf-8')
        return True
    return False


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    dirs = ['src', 'include']
    changed = 0
    for d in dirs:
        for ext in ('*.cpp', '*.h'):
            for f in sorted((root / d).rglob(ext)):
                if sort_includes_in_file(f):
                    print(f"  sorted: {f.relative_to(root)}")
                    changed += 1
    print(f"\n{changed} files changed")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
