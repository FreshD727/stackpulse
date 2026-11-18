"""Format owned C++ sources, or check their working-tree/staged contents."""

import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VERSION = "22.1.3"
SOURCE_ROOTS = ("apps", "include", "src", "tests", "tools")
SUFFIXES = {".cpp", ".hpp"}


def git(*args):
    return subprocess.check_output(
        ["git", "-c", "core.fsmonitor=false", *args], cwd=ROOT
    )


def owned(path):
    parts = Path(path).parts
    return parts[0] in SOURCE_ROOTS and Path(path).suffix in SUFFIXES


def formatter():
    executable = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if not executable:
        raise RuntimeError(
            f"Install clang-format {VERSION}: python -m pip install clang-format=={VERSION}\n"
            "Alternatively set CLANG_FORMAT to its executable path."
        )
    version = subprocess.check_output([executable, "--version"], text=True).strip()
    if not re.search(rf"\bversion {re.escape(VERSION)}\b", version):
        raise RuntimeError(f"Expected clang-format {VERSION}; found {version}")
    return executable


def install_hook():
    configured = subprocess.run(
        ["git", "-c", "core.fsmonitor=false", "config", "--get", "core.hooksPath"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if configured.returncode == 0:
        raise RuntimeError(
            "core.hooksPath is configured; add 'python tools/format_cpp.py --check --staged' "
            "to your existing hook workflow instead."
        )
    if configured.returncode != 1:
        raise RuntimeError("Could not inspect core.hooksPath")
    hook_dir = Path(git("rev-parse", "--git-path", "hooks").decode().strip())
    if not hook_dir.is_absolute():
        hook_dir = ROOT / hook_dir
    hook = hook_dir / "pre-commit"
    if hook.exists():
        raise RuntimeError(f"Hook already exists; leaving it untouched: {hook}")
    executable = Path(formatter()).resolve().as_posix()
    hook_dir.mkdir(parents=True, exist_ok=True)
    hook.write_text(
        '#!/bin/sh\n'
        '# Opt-in StackPulse formatting check; checks the staged snapshot.\n'
        f'CLANG_FORMAT={shlex.quote(executable)} '
        f'exec {shlex.quote(Path(sys.executable).as_posix())} '
        'tools/format_cpp.py --check --staged\n',
        encoding="utf-8",
        newline="\n",
    )
    hook.chmod(hook.stat().st_mode | 0o111)
    print(f"Installed local hook: {hook}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report without editing")
    parser.add_argument("--staged", action="store_true", help="check staged C++ contents")
    parser.add_argument("--install-hook", action="store_true", help="opt in to local hook")
    args = parser.parse_args()
    if args.install_hook:
        install_hook()
        return 0
    if args.staged and not args.check:
        parser.error("--staged requires --check; staged files are never rewritten")
    if args.staged:
        names = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z")
    else:
        names = git("ls-files", "-z", "--", *SOURCE_ROOTS)
    paths = sorted(
        {
            os.fsdecode(name)
            for name in names.split(b"\0")
            if name and owned(os.fsdecode(name))
        }
    )
    if not paths:
        print("No owned C++ files to check.")
        return 0
    executable = formatter()
    failures = []
    for name in paths:
        path = ROOT / name
        before = git("show", f":{name}") if args.staged else path.read_bytes()
        after = subprocess.check_output(
            [executable, "--style=file", f"--assume-filename={path}"],
            input=before,
            cwd=ROOT,
        )
        if before.replace(b"\r\n", b"\n") == after.replace(b"\r\n", b"\n"):
            continue
        if args.check:
            failures.append(name)
            print(f"Needs formatting: {name}")
        else:
            path.write_bytes(after)
    if failures:
        print("Run python tools/format_cpp.py, review, and stage the formatted changes.")
        return 1
    print(f"{'Checked' if args.check else 'Formatted'} {len(paths)} C++ files.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Formatting error: {error}", file=sys.stderr)
        sys.exit(2)
