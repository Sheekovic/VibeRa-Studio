"""Popup menu item IDs must be unique inside one shown menu tree.

A PopupMenu and its submenus share one result ID space: two items with the same literal ID run the
same handler branch (the first match), so one of them silently does the wrong thing. This scans the
sources for `x.addItem(<literal>, ...)` between a top-level `juce::PopupMenu menu` declaration and the
`showMenuAsync` that shows it and reports literal IDs used twice (fatal). A computed run written as
`<base> + <name>` whose loop has a literal bound (`name < N`, `name <= N`) is expanded and compared too;
with an unknown bound, IDs within 7 after the base are only listed for review (not fatal).

usage: python scripts/check-menu-ids.py   (exit 1 when a duplicate is found)
"""
import collections
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
ITEM = re.compile(r"\b\w+\.addItem\(\s*(\d+)\s*,")
TOP = re.compile(r"^\s*juce::PopupMenu\s+(?:menu|m)\b")
RUN = re.compile(r"\b\w+\.addItem\(\s*(\d+)\s*\+\s*(?:int\()?\s*(\w+)\b(?!\s*\*)")  # `base + i * 10 + k` is not a simple run.
found, review = [], []
for path in sorted((ROOT / "src").rglob("*")):
    if path.suffix not in (".h", ".cpp", ".inc"):
        continue
    lines = path.read_text(encoding="utf-8-sig", errors="replace").split("\n")
    seen = collections.defaultdict(list)
    runs = []
    for number, line in enumerate(lines, 1):
        if TOP.search(line):
            seen = collections.defaultdict(list)
            runs = []
        for match in ITEM.finditer(line):
            seen[int(match.group(1))].append(number)
        for match in RUN.finditer(line):
            base, name = int(match.group(1)), match.group(2)
            bound = None  # The loop header is on this line or one of the three before it.
            for earlier in lines[max(0, number - 4):number]:
                loop = re.search(r"\b" + re.escape(name) + r"\s*(<=?)\s*(\d+)\s*;", earlier)
                start = re.search(r"\b" + re.escape(name) + r"\s*=\s*(\d+)\s*;", earlier)
                if loop:
                    last = int(loop.group(2)) - (loop.group(1) == "<")
                    bound = (base + (int(start.group(1)) if start else 0), base + last)
            runs.append((base, bound, number))
        if "showMenuAsync" in line or re.search(r"\b(?:menu|m)\.show\(", line):
            for ident, places in sorted(seen.items()):
                # The same ID in the two arms of an if/else is one item, not two.
                exclusive = any(re.fullmatch(r"\s*(?:\}\s*)?else\s*\{?\s*", lines[n - 1])
                                for n in range(places[0], places[-1]))
                if ident and len(places) > 1 and not exclusive:
                    found.append(f"{path.relative_to(ROOT).as_posix()}: item ID {ident} at lines {places}")
            name = path.relative_to(ROOT).as_posix()
            for base, bound, at in runs:
                others = [(i, p[0]) for i, p in seen.items()] + [(b, n) for b, _, n in runs if n != at]
                for ident, where in others:
                    if bound and bound[0] <= ident <= bound[1]:
                        found.append(f"{name}: item ID {ident} (line {where}) is inside the run {bound[0]}..{bound[1]} (line {at})")
                    elif not bound and base < ident <= base + 7:
                        review.append(f"{name}: item ID {ident} (line {where}) follows the open run {base}+ (line {at})")
            seen = collections.defaultdict(list)
            runs = []
for line in review:
    print("review: " + line)
for line in found:
    print(line)
print(f"{len(found)} duplicate menu item ID(s)")
sys.exit(1 if found else 0)
