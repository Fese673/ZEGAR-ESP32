from pathlib import Path

def print_tree(path: Path, indent: int = 0, max_depth: int = 2):
    if indent > max_depth:
        return
    prefix = '    ' * indent
    if path.is_dir():
        print(f"{prefix}{path.name}/")
        for child in sorted(path.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
            print_tree(child, indent + 1, max_depth)
    else:
        print(f"{prefix}{path.name}")


def main():
    root = Path(__file__).resolve().parent.parent
    for top in ['include', 'src']:
        top_path = root / top
        if top_path.exists():
            print(f"{top}/")
            for child in sorted(top_path.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
                print_tree(child, 1, max_depth=2)
            print()
        else:
            print(f"{top}/ not found")


if __name__ == '__main__':
    main()
