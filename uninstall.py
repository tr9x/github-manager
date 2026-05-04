#!/usr/bin/env python3
"""uninstall.py — remove GitHub Manager.

Removes the installed binary and the local build directory. With
--purge, also wipes the application's QSettings file. Tokens stored in
the system keyring are never auto-removed; you can clear them by signing
out from inside the app.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Sequence

REPO_ROOT = Path(__file__).resolve().parent
BUILD_DIR = REPO_ROOT / "build"

GREEN = "\033[1;32m"
BLUE  = "\033[1;34m"
RED   = "\033[1;31m"
YELL  = "\033[1;33m"
RESET = "\033[0m"

def info(m: str) -> None: print(f"{BLUE}=>{RESET} {m}")
def ok(m: str)   -> None: print(f"{GREEN}✓{RESET} {m}")
def warn(m: str) -> None: print(f"{YELL}!{RESET} {m}")
def fail(m: str) -> None: print(f"{RED}✗ {m}{RESET}", file=sys.stderr)

def run(cmd: Sequence[str], *, check: bool = True) -> int:
    print(f"   $ {' '.join(cmd)}")
    return subprocess.run(cmd, check=check).returncode

# ---------------------------------------------------------------------------

DEFAULT_PREFIXES = [
    Path.home() / ".local",
    Path("/usr/local"),
]

INSTALLED_FILES = [
    "bin/github-manager",
    "share/applications/github-manager.desktop",
]

def remove_path(path: Path, *, sudo: bool) -> None:
    if not path.exists() and not path.is_symlink():
        return
    info(f"Removing {path}")
    cmd = ["sudo", "rm", "-rf", str(path)] if sudo else ["rm", "-rf", str(path)]
    if sudo:
        run(cmd, check=False)
    else:
        try:
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
            else:
                path.unlink(missing_ok=True)
        except PermissionError:
            warn(f"Permission denied; retrying with sudo")
            run(["sudo", "rm", "-rf", str(path)], check=False)

def uninstall_from(prefix: Path) -> bool:
    """Returns True if anything was removed under `prefix`."""
    removed = False
    sudo = not os.access(prefix, os.W_OK) and prefix == Path("/usr/local")
    for rel in INSTALLED_FILES:
        target = prefix / rel
        if target.exists() or target.is_symlink():
            remove_path(target, sudo=sudo)
            removed = True
    return removed

def clean_build() -> None:
    if BUILD_DIR.exists():
        info(f"Removing build directory {BUILD_DIR}")
        shutil.rmtree(BUILD_DIR)
        ok("Build directory cleaned.")

def purge_settings() -> None:
    cfg = Path.home() / ".config" / "github-manager"
    if cfg.exists():
        info(f"Purging settings at {cfg}")
        shutil.rmtree(cfg)
        ok("Settings removed.")
    else:
        info("No settings directory to purge.")

# ---------------------------------------------------------------------------

def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--prefix", type=Path,
                   help="explicit install prefix to clean (defaults: ~/.local and /usr/local)")
    p.add_argument("--purge", action="store_true",
                   help="also remove configuration files in ~/.config/github-manager")
    p.add_argument("--keep-build", action="store_true",
                   help="don't delete the build/ directory")
    args = p.parse_args(argv)

    prefixes = [args.prefix] if args.prefix else DEFAULT_PREFIXES

    found_anything = False
    for pref in prefixes:
        if uninstall_from(pref):
            ok(f"Removed installed files from {pref}")
            found_anything = True
    if not found_anything:
        warn("No installed binaries found in default prefixes.")

    if not args.keep_build:
        clean_build()

    if args.purge:
        purge_settings()
        warn("Tokens in your system keyring have NOT been removed.")
        warn("To clear them, run the app and use the 'Sign out' menu, "
             "or use seahorse / KWallet to delete entries with schema "
             "'local.github-manager.Token'.")

    ok("Uninstall complete.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        fail("Interrupted.")
        sys.exit(130)
