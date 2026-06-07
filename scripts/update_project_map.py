#!/usr/bin/env python3
"""
Scan repository directories and update the corresponding sections in Project-Map.md.

Usage:
    python scripts/update_project_map.py               # update in-place
    python scripts/update_project_map.py --dry-run      # print diff without writing
    python scripts/update_project_map.py --validate     # check-only, exit 1 if stale
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding='utf-8')
except AttributeError:
    pass


# ── Tree generation ───────────────────────────────────────────────────

EXCLUDE_DIRS = {'__pycache__', '.pio', '.venv', '.git', '.vscode'}
EXCLUDE_FILES = set()

def format_tree(root: Path, max_depth: int = 2) -> list[str]:
    """Return tree lines for a directory (including the root name).
    Skips common artifacts like __pycache__/.
    """
    lines: list[str] = []

    def walk(path: Path, depth: int, prefix: str, is_last: bool) -> None:
        entries = sorted(path.iterdir(), key=lambda e: (not e.is_dir(), e.name.lower()))
        for idx, entry in enumerate(entries):
            if entry.is_dir() and entry.name in EXCLUDE_DIRS:
                continue
            if not entry.is_dir() and entry.name in EXCLUDE_FILES:
                continue
            last = idx == len(entries) - 1
            branch = '\u2514\u2500\u2500' if last else '\u251c\u2500\u2500'
            name = entry.name + ('/' if entry.is_dir() else '')
            lines.append(f"{prefix}{branch} {name}")
            if entry.is_dir() and depth < max_depth:
                child_prefix = f"{prefix}{'    ' if last else '\u2502   '}"
                walk(entry, depth + 1, child_prefix, last)

    lines.append(f"{root.name}/")
    walk(root, 0, '', True)
    return lines


# ── Section detection ─────────────────────────────────────────────────

# A section header line: directory name ending with "/" at depth 0 (no tree prefix)
SECTION_RE = re.compile(r'^(?P<name>\w[\w.\-]*)/\s*(?:#.*)?$')

def is_section_header(line: str) -> str | None:
    """If line is a section header, return the section name. Otherwise None."""
    m = SECTION_RE.match(line)
    if not m:
        return None
    # Must not start with tree-drawing chars
    if line.startswith(('\u2502', '\u251c', '\u2514')):
        return None
    return m.group('name')


def find_sections(lines: list[str]) -> list[tuple[str, int, int]]:
    """Return list of (name, start_line_idx, end_line_idx) for each section found.
    
    A section starts at its header line and ends at the next section header
    or EOF (exclusive end).  The end index is the first non-section line.
    """
    sections: list[tuple[str, int, int]] = []
    i = 0
    while i < len(lines):
        name = is_section_header(lines[i])
        if name is not None:
            start = i
            i += 1
            while i < len(lines):
                if is_section_header(lines[i]) is not None:
                    break
                i += 1
            sections.append((name, start, i))
        else:
            i += 1
    return sections


# ── Updater ───────────────────────────────────────────────────────────

def update_project_map(map_path: Path, sections: list[str],
                       max_depth: int = 2, dry_run: bool = False) -> list[str]:
    """Update listed sections in Project-Map.md from actual filesystem.
    
    Returns list of section names that were changed (empty if none).
    """
    repo_root = map_path.resolve().parent

    with open(map_path, 'r', encoding='utf-8') as f:
        original_lines = f.readlines()

    lines = original_lines[:]
    changed: list[str] = []

    found_sections = find_sections(lines)

    # Process bottom-up so index shifts don't invalidate earlier positions
    for name, start, end in reversed(found_sections):
        if name not in sections:
            continue

        dir_path = repo_root / name
        if not dir_path.exists():
            print(f"WARNING: section '{name}/' directory not found", file=sys.stderr)
            continue

        new_tree = format_tree(dir_path, max_depth)
        # Add trailing newline (format_tree returns lines without newlines)
        new_block = '\n'.join(new_tree) + '\n'

        old_block = ''.join(lines[start:end])

        # Normalize trailing newlines/blank lines for comparison
        # (sections in the file may have blank-line separators between them
        #  which get captured in lines[start:end])
        if new_block == old_block or new_block.strip() == old_block.strip():
            continue

        changed.append(name)
        replacement = [line + '\n' for line in new_tree]  # include trailing newlines
        # Ensure blank line after section (old format had one between sections)
        if not replacement[-1].isspace():
            replacement.append('\n')

        if dry_run:
            continue  # skip diff printing (box-drawing chars break Windows console)
        else:
            lines[start:end] = replacement

    if not changed:
        return []

    if dry_run:
        return changed

    with open(map_path, 'w', encoding='utf-8', newline='') as f:
        f.writelines(lines)
    return changed


# ── CLI ───────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(
        description='Update Project-Map.md sections from actual filesystem state.'
    )
    parser.add_argument(
        '--dry-run', '-n', action='store_true',
        help='Print diff without modifying Project-Map.md'
    )
    parser.add_argument(
        '--validate', action='store_true',
        help='Check-only: exit 1 if any tracked section is stale'
    )
    parser.add_argument(
        '--map', type=Path, default=Path('Project-Map.md'),
        help='Path to Project-Map.md (default: ./Project-Map.md)'
    )
    parser.add_argument(
        '--section', '-s', action='append', default=None,
        help='Sections to check (default: include, src, scripts)'
    )
    parser.add_argument(
        '--max-depth', type=int, default=2,
        help='Maximum tree depth (default: 2)'
    )
    args = parser.parse_args()
    sections = args.section if args.section else ['include', 'src', 'scripts']

    map_path = args.map.resolve()
    if not map_path.exists():
        print(f"ERROR: {map_path} not found", file=sys.stderr)
        return 1

    if args.validate:
        changed = update_project_map(map_path, sections, args.max_depth, dry_run=True)
        if changed:
            names = ', '.join(changed)
            print(f"OUTDATED ({names}) — run without --validate to update", file=sys.stderr)
            return 1
        print("OK: all tracked sections match actual tree")
        return 0

    changed = update_project_map(map_path, sections, args.max_depth, args.dry_run)

    if args.dry_run:
        if not changed:
            print("No changes needed.")
        return 0

    if changed:
        print(f"Updated sections: {', '.join(changed)}")
    else:
        print("Already up to date.")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
