"""scan_missing_refs.py - find /Game/ package refs inside .uasset files that have no
file on disk under the Content folder (override: WRECKTIDE_CONTENT). Pure binary string scan (no UE needed).

Usage:
  py scan_missing_refs.py <uasset-or-dir> [<uasset-or-dir> ...]

Method: extract ASCII strings matching /Game/<pkg> from each uasset, normalize to a
package path (strip object suffixes after '.'), check Content\\<pkg>.uasset or .umap
exists. Report-only. False positives possible (soft refs, None-entries, redirector
text) - human judges the output; this LOCATES, it does not decide.
"""
import os
import re
import sys
from pathlib import Path

CONTENT = Path(os.environ.get("WRECKTIDE_CONTENT", Path(__file__).resolve().parent.parent / "Content"))
PKG_RE = re.compile(rb"/Game/[A-Za-z0-9_/\-]+")

def pkg_exists(pkg: str) -> bool:
    rel = pkg[len("/Game/"):]
    return (CONTENT / (rel + ".uasset")).exists() or (CONTENT / (rel + ".umap")).exists()

def scan_file(f: Path):
    data = f.read_bytes()
    pkgs = set()
    for m in PKG_RE.finditer(data):
        s = m.group().decode("ascii", "ignore")
        # normalize: package path only (no trailing garbage handled by regex already)
        pkgs.add(s)
    missing = sorted(p for p in pkgs if not pkg_exists(p))
    return sorted(pkgs), missing

def main():
    targets = []
    for a in sys.argv[1:]:
        p = Path(a)
        if p.is_dir():
            targets += sorted(p.rglob("*.uasset"))
        elif p.exists():
            targets.append(p)
        else:
            print(f"SKIP (not found): {a}")
    any_missing = False
    for f in targets:
        pkgs, missing = scan_file(f)
        if missing:
            any_missing = True
            print(f"\n== {f.name} ({len(pkgs)} refs, {len(missing)} MISSING) ==")
            for m in missing:
                print(f"  MISSING {m}")
    if not any_missing:
        print("ALL REFS RESOLVE ON DISK")

if __name__ == "__main__":
    main()
