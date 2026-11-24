#!/usr/bin/env python3
"""Apply, inspect or reverse the WG-CW v2.2.6 LCD patch."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

DISPLAY_FILES = (
    "main/display/lcd_display.cc",
    "main/display/lcd_display.h",
    "main/display/wg_cw_faces.h",
)
PATCH = Path(__file__).resolve().with_name("bigfaces.patch")


def git(root, *args):
    return subprocess.run(
        ["git", "-C", str(root), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )


def patch_args(root):
    line_endings = set()
    for name in DISPLAY_FILES[:2]:
        data = (root / name).read_bytes()
        if b"\r\n" in data:
            line_endings.add("crlf")
        if b"\n" in data.replace(b"\r\n", b""):
            line_endings.add("lf")
    if len(line_endings) != 1:
        raise ValueError("LCD source files have inconsistent line endings.")
    autocrlf = "true" if "crlf" in line_endings else "false"
    return ["-c", f"core.autocrlf={autocrlf}", "apply", "--whitespace=nowarn",
            *(f"--include={name}" for name in DISPLAY_FILES)]


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Xiaozhi repository root")
    parser.add_argument("--check", action="store_true", help="check only, without writing")
    parser.add_argument("--rollback", action="store_true", help="reverse this patch")
    options = parser.parse_args()
    root = options.repo.resolve()
    top = git(root, "rev-parse", "--show-toplevel")
    if top.returncode or Path(top.stdout.strip()).resolve() != root:
        raise ValueError("Run from the repository root or specify it with --repo.")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    if not re.search(r'^set\(PROJECT_VER\s+"2\.2\.6"\)', cmake, re.MULTILINE):
        raise ValueError("This patch requires Xiaozhi v2.2.6.")
    if not PATCH.is_file():
        raise ValueError("Keep bigfaces.patch next to apply_bigfaces.py.")

    args = patch_args(root)
    forward = git(root, *args, "--check", str(PATCH))
    reverse = git(root, *args, "--check", "--reverse", str(PATCH))
    chosen = reverse if options.rollback else forward
    opposite = forward if options.rollback else reverse
    if chosen.returncode:
        if not opposite.returncode:
            print("Already rolled back." if options.rollback else "Patch already applied.")
            return
        raise ValueError("Patch context conflicts with this checkout. No files changed.\n"
                         + chosen.stderr.strip())
    if options.check:
        print("Rollback check passed." if options.rollback else "Apply check passed.")
        return
    action = ["--reverse"] if options.rollback else []
    result = git(root, *args, *action, str(PATCH))
    if result.returncode:
        raise ValueError(result.stderr.strip())
    print("Big-face patch rolled back." if options.rollback else "Big-face patch applied.")
    print("Affected paths:")
    for name in DISPLAY_FILES:
        print("  " + name)


if __name__ == "__main__":
    try:
        run()
    except (OSError, ValueError) as error:
        sys.exit(f"ERROR: {error}")
