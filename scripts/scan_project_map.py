from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Iterable

TREE_PREFIX_PATTERN = re.compile(r"^(?P<indent>(?: {4}|│   )*)(?P<branch>[├└])── (?P<name>.+)$")
COMMENT_PATTERN = re.compile(r"\s+#.*$")
EXCLUDE_DIRS = {'__pycache__', '.pio', '.venv', '.git', '.vscode'}


def collect_tree(root: Path, max_depth: int = 2) -> set[str]:
    paths: set[str] = set()

    def walk(path: Path, rel_path: Path, depth: int) -> None:
        if depth > max_depth:
            return
        entries = sorted(path.iterdir(), key=lambda entry: (not entry.is_dir(), entry.name.lower()))
        for entry in entries:
            if entry.is_dir() and entry.name in EXCLUDE_DIRS:
                continue
            entry_rel = rel_path / entry.name
            if entry.is_dir():
                paths.add(f"{entry_rel.as_posix()}/")
                walk(entry, entry_rel, depth + 1)
            else:
                paths.add(entry_rel.as_posix())

    walk(root, Path('.'), 0)
    return paths


def format_tree(root: Path, max_depth: int = 2) -> Iterable[str]:
    def _walk(path: Path, depth: int, prefix: str, last: bool) -> Iterable[str]:
        entries = sorted(path.iterdir(), key=lambda entry: (not entry.is_dir(), entry.name.lower()))
        for index, entry in enumerate(entries):
            if entry.is_dir() and entry.name in EXCLUDE_DIRS:
                continue
            is_last = index == len(entries) - 1
            branch = '└──' if is_last else '├──'
            line = f"{prefix}{branch} {entry.name}"
            yield line
            if entry.is_dir() and depth < max_depth:
                child_prefix = f"{prefix}{'    ' if is_last else '│   '}"
                yield from _walk(entry, depth + 1, child_prefix, is_last)

    yield f"{root.name}/"
    yield from _walk(root, 0, '', True)


def parse_map_section(lines: list[str], section_header: str) -> set[str] | None:
    header = section_header.rstrip('/') + '/'
    start_index = None
    for index, line in enumerate(lines):
        if line.strip() == header:
            start_index = index + 1
            break
    if start_index is None:
        return None

    parsed: set[str] = set()
    stack: list[str] = []
    for line in lines[start_index:]:
        if not line.strip():
            break
        match = TREE_PREFIX_PATTERN.match(line)
        if not match:
            continue
        indent = match.group('indent')
        depth = len(indent) // 4
        name = COMMENT_PATTERN.sub('', match.group('name')).strip()
        if not name:
            continue
        if name.endswith('/'):
            dir_name = name[:-1]
            stack = stack[:depth] + [dir_name]
            parsed.add(f"{'/'.join(stack)}/")
        else:
            path = stack[:depth] + [name]
            parsed.add('/'.join(path))
    return parsed


def validate_section(root: Path, map_lines: list[str], section: str, max_depth: int = 2) -> bool:
    section_path = root / section
    print(f"Validating section: {section}/")
    if not section_path.exists():
        print(f"  ERROR: directory not found: {section_path}")
        return False

    actual = collect_tree(section_path, max_depth=max_depth)
    documented = parse_map_section(map_lines, section)
    if documented is None:
        print(f"  WARNING: section '{section}/' not found in Project-Map.md")
        print("  Actual tree:\n" + '\n'.join(sorted(actual)))
        return False

    missing = sorted(actual - documented)
    extra = sorted(documented - actual)
    if not missing and not extra:
        print("  OK: documentation matches actual tree")
        return True

    if missing:
        print("  Missing from documentation:")
        for item in missing:
            print(f"    + {item}")
    if extra:
        print("  Extra entries in documentation:")
        for item in extra:
            print(f"    - {item}")

    return False


def load_project_map(path: Path) -> list[str]:
    if not path.exists():
        raise FileNotFoundError(f"Project map not found at {path}")
    return path.read_text(encoding='utf-8').splitlines()


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Scan repository structure and validate Project-Map.md sections for include, src, and scripts.'
    )
    parser.add_argument(
        '--root',
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help='Repository root directory',
    )
    parser.add_argument(
        '--section',
        '-s',
        action='append',
        default=None,
        help='Top-level directory sections to validate or print (default: include, src, scripts)',
    )
    parser.add_argument(
        '--max-depth',
        type=int,
        default=2,
        help='Maximum tree depth to scan and compare',
    )
    parser.add_argument(
        '--validate',
        action='store_true',
        help='Validate selected sections against Project-Map.md',
    )
    parser.add_argument(
        '--map',
        type=Path,
        default=Path(__file__).resolve().parent.parent / 'Project-Map.md',
        help='Path to Project-Map.md',
    )
    args = parser.parse_args()
    sections = args.section if args.section else ['include', 'src', 'scripts']

    if args.validate:
        map_lines = load_project_map(args.map)
        success = True
        for section in sections:
            success &= validate_section(args.root, map_lines, section, max_depth=args.max_depth)
        return 0 if success else 1

    for section in sections:
        section_root = args.root / section
        if not section_root.exists():
            print(f"{section}/ not found")
            continue
        for line in format_tree(section_root, max_depth=args.max_depth):
            print(line)
        print()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
