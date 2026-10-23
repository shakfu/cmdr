#!/usr/bin/env python3
"""Build cmdr and install it into PREFIX, with the examples' packages.

The binary is copied to PREFIX/bin. Example packages are symlinked into
PREFIX/lib, where `import` finds them, so edits in the repository take effect.
Extensions are compiled into cmdr and need no links.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OPTIONAL = ["snd", "midi"]  # As the top-level CMakeLists.txt lists them.


def link(src, dest):
    """Symlink DEST to SRC, replacing only an earlier symlink."""
    if dest.exists() and not dest.is_symlink():
        print(f"skipped {dest}: exists and is not a symlink", file=sys.stderr)
        return
    if dest.is_symlink():
        dest.unlink()
    dest.symlink_to(src)
    print(f"linked {dest} -> {src}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("prefix", nargs="?", type=Path, default=Path.home() / ".cmdr",
                        help="directory to install into (default: %(default)s)")
    parser.add_argument("-w", "--with", dest="extensions", action="append", default=[],
                        choices=OPTIONAL, metavar="NAME",
                        help=f"build an optional extension into cmdr: {', '.join(OPTIONAL)}; "
                             "repeatable. Configure fails if its libraries are missing")
    parser.add_argument("-b", "--build-dir", default="build",
                        help="build directory, relative to the repository (default: %(default)s)")
    args = parser.parse_args(argv)

    # Every option is set, so an earlier configure of the build directory
    # cannot add an extension that was not asked for.
    flags = " ".join(f"-DCMDR_{ext.upper()}={'ON' if ext in args.extensions else 'OFF'}"
                     for ext in OPTIONAL)
    status = subprocess.call(["make", "-C", str(ROOT), f"BUILD_DIR={args.build_dir}",
                              f"CMAKE_FLAGS={flags}"])
    if status:
        return status

    bin_dir, lib_dir = args.prefix / "bin", args.prefix / "lib"
    bin_dir.mkdir(parents=True, exist_ok=True)
    lib_dir.mkdir(parents=True, exist_ok=True)
    # Copy, then rename, since a running cmdr makes its file unwritable.
    tmp = bin_dir / ".cmdr.tmp"
    shutil.copy2(ROOT / args.build_dir / "cmdr", tmp)
    tmp.replace(bin_dir / "cmdr")
    print(f"installed {bin_dir / 'cmdr'}")

    # Packages, examples/X/NAME/__init__.cmdr, go in lib; entry points,
    # examples/X/X.cmdr, go in bin.
    for init in sorted(ROOT.glob("examples/*/*/__init__.cmdr")):
        link(init.parent, lib_dir / init.parent.name)
    for example in sorted(p for p in (ROOT / "examples").iterdir() if p.is_dir()):
        entry = example / f"{example.name}.cmdr"
        if entry.is_file() and os.access(entry, os.X_OK):
            link(entry, bin_dir / entry.name)

    if str(bin_dir) not in os.environ.get("PATH", "").split(os.pathsep):
        print(f"add {bin_dir} to PATH")
    return 0


if __name__ == "__main__":
    sys.exit(main())
