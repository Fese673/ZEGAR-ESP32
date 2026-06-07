#!/usr/bin/env python3
import pathlib
import re

root = pathlib.Path(__file__).resolve().parent.parent
files = list(root.glob('src/**/*.cpp')) + list(root.glob('src/**/*.c')) + list(root.glob('include/**/*.cpp')) + list(root.glob('include/**/*.c'))
pattern = re.compile(r'^(?:static|static\s+inline)\s+(?:[\w:\<\>\*&\s]+?)\s+(\w+)\s*\([^;\n]*\)\s*\{', re.MULTILINE)

all_text = '\n'.join([p.read_text(encoding='utf-8', errors='ignore') for p in files])
results = []
for path in files:
    text = path.read_text(encoding='utf-8', errors='ignore')
    for name in pattern.findall(text):
        count = all_text.count(name)
        if count <= 1:
            results.append((path.relative_to(root), name, count))

print('Candidates static functions with <=1 references:')
for p, name, count in sorted(results):
    print(f'{p}:{name} refs={count}')
print('Total', len(results))
