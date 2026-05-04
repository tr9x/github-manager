#!/usr/bin/env python3
"""install.py — build and install GitHub Manager.

Detects the local distribution (Arch, Debian/Ubuntu, Fedora) and uses its
package manager to install build dependencies, then configures and builds
the project with CMake/Ninja, and finally installs the binary.

Defaults to a per-user install at ~/.local (no sudo). Pass --system for
/usr/local. Pass --no-deps to skip the dependency step.

Tested on:
    Arch Linux (current)
    Ubuntu 24.04
    Fedora 40
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Sequence

REPO_ROOT = Path(__file__).resolve().parent
BUILD_DIR = REPO_ROOT / "build"

# ANSI helpers ---------------------------------------------------------------
GREEN = "\033[1;32m"
BLUE  = "\033[1;34m"
RED   = "\033[1;31m"
YELL  = "\033[1;33m"
RESET = "\033[0m"

def info(msg: str)  -> None: print(f"{BLUE}=>{RESET} {msg}")
def ok(msg: str)    -> None: print(f"{GREEN}✓{RESET} {msg}")
def warn(msg: str)  -> None: print(f"{YELL}!{RESET} {msg}")
def fail(msg: str)  -> None: print(f"{RED}✗ {msg}{RESET}", file=sys.stderr)

# ---------------------------------------------------------------------------

def run(cmd: Sequence[str], *, check: bool = True, cwd: Path | None = None) -> int:
    print(f"   $ {' '.join(cmd)}")
    proc = subprocess.run(cmd, cwd=cwd)
    if check and proc.returncode != 0:
        fail(f"command failed (exit {proc.returncode})")
        sys.exit(proc.returncode)
    return proc.returncode

def have(cmd: str) -> bool:
    return shutil.which(cmd) is not None

def detect_distro() -> str:
    """Return one of: arch, debian, fedora, unknown."""
    osr = Path("/etc/os-release")
    if not osr.exists():
        return "unknown"
    fields: dict[str, str] = {}
    for line in osr.read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            fields[k] = v.strip().strip('"')
    ids = [fields.get("ID", "")] + fields.get("ID_LIKE", "").split()
    for tok in ids:
        if tok in {"arch", "manjaro", "endeavouros"}:                    return "arch"
        if tok in {"debian", "ubuntu", "linuxmint", "pop"}:              return "debian"
        if tok in {"fedora", "rhel", "centos", "rocky", "almalinux"}:    return "fedora"
    return "unknown"

# ---------------------------------------------------------------------------

ARCH_DEPS = [
    "base-devel", "cmake", "ninja", "pkgconf",
    "qt6-base", "qt6-tools",
    "libgit2", "libsecret",
    "git",
]

DEBIAN_DEPS = [
    "build-essential", "cmake", "ninja-build", "pkg-config",
    "qt6-base-dev", "qt6-tools-dev", "qt6-tools-dev-tools",
    "libgl1-mesa-dev",                     # Qt6 OpenGL link dep
    "libgit2-dev", "libsecret-1-dev",
    "git",
]

FEDORA_DEPS = [
    "@development-tools", "cmake", "ninja-build", "pkgconf-pkg-config",
    "qt6-qtbase-devel", "qt6-qttools-devel",
    "libgit2-devel", "libsecret-devel",
    "git",
]

def install_deps(distro: str) -> None:
    info(f"Installing build dependencies for {distro}…")
    if distro == "arch":
        run(["sudo", "pacman", "-S", "--needed", "--noconfirm", *ARCH_DEPS])
    elif distro == "debian":
        run(["sudo", "apt-get", "update"])
        run(["sudo", "apt-get", "install", "-y", *DEBIAN_DEPS])
    elif distro == "fedora":
        run(["sudo", "dnf", "install", "-y", *FEDORA_DEPS])
    else:
        warn("Unknown distribution. Install these manually:")
        print("  - CMake >= 3.21, Ninja, a C++20 compiler, pkg-config")
        print("  - Qt 6 (Core, Gui, Widgets, Network, Concurrent)")
        print("  - libgit2, libsecret-1")
        print()
        if input("Continue anyway? [y/N] ").strip().lower() != "y":
            sys.exit(1)
    ok("Dependencies satisfied.")

# ---------------------------------------------------------------------------

def configure_and_build(install_prefix: Path, jobs: int) -> None:
    info(f"Configuring (prefix = {install_prefix})…")
    BUILD_DIR.mkdir(exist_ok=True)
    cmake_cmd = [
        "cmake",
        "-S", str(REPO_ROOT),
        "-B", str(BUILD_DIR),
        f"-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={install_prefix}",
    ]
    if have("ninja"):
        cmake_cmd += ["-G", "Ninja"]
    run(cmake_cmd)

    info(f"Building (using {jobs} jobs)…")
    run(["cmake", "--build", str(BUILD_DIR), "--parallel", str(jobs)])
    ok("Build complete.")

def install(install_prefix: Path, system: bool) -> None:
    info(f"Installing into {install_prefix}…")
    install_cmd = ["cmake", "--install", str(BUILD_DIR)]
    if system:
        install_cmd = ["sudo", *install_cmd]
    run(install_cmd)

    bin_path = install_prefix / "bin" / "github-manager"
    if bin_path.exists():
        ok(f"Installed: {bin_path}")
    else:
        warn(f"Expected binary at {bin_path} but didn't find it.")

    if not system:
        # Make sure ~/.local/bin is in PATH; warn if not.
        user_bin = str(install_prefix / "bin")
        if user_bin not in os.environ.get("PATH", "").split(os.pathsep):
            warn(f"{user_bin} is not in your PATH. Add this to your shell rc:")
            print(f"      export PATH=\"{user_bin}:$PATH\"")

# ---------------------------------------------------------------------------

def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--system", action="store_true",
                   help="install to /usr/local (requires sudo)")
    p.add_argument("--no-deps", action="store_true",
                   help="skip the dependency-install step")
    p.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2,
                   help="parallel build jobs")
    p.add_argument("--prefix", type=Path,
                   help="custom install prefix (overrides --system)")
    args = p.parse_args(argv)

    if platform.system() != "Linux":
        fail(f"Unsupported platform: {platform.system()}. Linux only.")
        return 1

    if args.prefix is not None:
        prefix = args.prefix.resolve()
        system = False
    elif args.system:
        prefix = Path("/usr/local")
        system = True
    else:
        prefix = Path.home() / ".local"
        system = False

    if not args.no_deps:
        install_deps(detect_distro())
    else:
        info("Skipping dependency install (--no-deps).")

    configure_and_build(prefix, args.jobs)
    install(prefix, system)

    ok("All done. Launch with:  github-manager")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        fail("Interrupted.")
        sys.exit(130)
