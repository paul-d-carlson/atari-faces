#!/usr/bin/env python3
"""Turn a runner --profile dump into cycles per function.

    tools/profile.py PROFILE MAP_OR_DBG [N]

PROFILE lines are "ADDR CYCLES COUNT" (hex address, decimal numbers)
written by the test runners' --profile option. The second file is the
ld65 map (exported symbols only) or, better, the --dbgfile of the same
image, which also names static C functions. Each address is charged to
the nearest symbol at or below it.
"""
import bisect
import re
import sys


def dbg_symbols(path):
    syms = []
    with open(path) as fh:
        for line in fh:
            if not line.startswith("sym\t"):
                continue
            name = re.search(r'name="([^"]+)"', line)
            val = re.search(r"val=0x([0-9A-Fa-f]+)", line)
            if name and val and "type=lab" in line and name.group(1).startswith("_"):
                syms.append((int(val.group(1), 16), name.group(1)))
    syms.sort()
    return syms


def symbols(map_path):
    if map_path.endswith(".dbg"):
        # C statics come from the debug file, runtime-library routines
        # (built without debug information) from the map beside it.
        merged = dict((a, n) for a, n in map_symbols(map_path[:-4] + ".map"))
        merged.update(dict(dbg_symbols(map_path)))
        return sorted(merged.items())
    return map_symbols(map_path)


def map_symbols(map_path):
    syms = []
    in_exports = False
    with open(map_path) as fh:
        for line in fh:
            if line.startswith("Exports list by value"):
                in_exports = True
                continue
            if in_exports and line.startswith("Imports list"):
                break
            if in_exports:
                for name, addr in re.findall(r"(\S+)\s+([0-9A-F]{6})\s+\S+", line):
                    # Linker-made names (__BSS_RUN__ etc.) are not code.
                    if not name.startswith("__"):
                        syms.append((int(addr, 16), name))
    syms.sort()
    return syms


def main():
    prof, map_path = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    syms = symbols(map_path)
    addrs = [a for a, _ in syms]
    totals = {}
    with open(prof) as fh:
        for line in fh:
            addr, cycles = line.split()[:2]
            a = int(addr, 16)
            i = bisect.bisect_right(addrs, a) - 1
            if a >= 0xC000:
                name = "(OS ROM)"
            elif a < 0x2000 and (i < 0 or syms[i][0] > a):
                name = "(OS RAM / DOS)"
            else:
                name = syms[i][1] if i >= 0 else "?"
            totals[name] = totals.get(name, 0) + int(cycles)
    grand = sum(totals.values())
    for name, c in sorted(totals.items(), key=lambda kv: -kv[1])[:top]:
        print(f"{c:12d} {100.0 * c / grand:5.1f}%  {name}")
    print(f"{grand:12d} total cycles ({grand / 1789773:.1f} s at 1.79 MHz)")


if __name__ == "__main__":
    main()
