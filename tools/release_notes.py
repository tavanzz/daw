#!/usr/bin/env python3
"""Writes a release's notes from the commits since the previous release.

    tools/release_notes.py <tag> [--unsigned-macos] [--unsigned-windows] [--throwaway-update-key]

A release (vX.Y.Z) lists the commits since the previous release, release candidates left
out of the count; a release candidate (vX.Y.Z-rc.N) those since the tag before it. Merge
commits and the "CI: ..." fixes of the build are left out: they say nothing to the people
using the app. Notes are Markdown, printed on standard output; the update panel shows
the same text.
"""

import argparse
import subprocess
import sys


def git(*arguments):
    return subprocess.run(["git", *arguments], check=True, capture_output=True, text=True).stdout.strip()


def previous_tag(tag):
    exclude = [] if "-" in tag else ["--exclude", "v*-*"]
    try:
        return git("describe", "--tags", "--abbrev=0", "--match", "v*", *exclude, f"{tag}^")
    except subprocess.CalledProcessError:
        return None  # the first release


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tag")
    parser.add_argument("--unsigned-macos", action="store_true")
    parser.add_argument("--unsigned-windows", action="store_true")
    parser.add_argument("--throwaway-update-key", action="store_true")
    parser.add_argument("--repository", default="https://github.com/tavanzz/daw")
    arguments = parser.parse_args()

    tag = arguments.tag
    previous = previous_tag(tag)
    span = f"{previous}..{tag}" if previous else tag
    subjects = [line for line in git("log", "--no-merges", "--reverse", "--format=%s", span).splitlines()
                if line and not line.startswith("CI:")]
    date = git("log", "-1", "--format=%cs", tag)

    version = tag[1:] if tag.startswith("v") else tag
    lines = [f"DAW {version}, {date}."]
    if "-" in version:
        lines.append("A release candidate: a version to try before everyone gets it.")
    lines.append("")

    warnings = []
    if arguments.unsigned_macos:
        warnings.append("**macOS**: this build is not signed with an Apple Developer ID. The first time, macOS "
                        "refuses to open it: open System Settings > Privacy & Security and choose Open Anyway.")
    if arguments.unsigned_windows:
        warnings.append("**Windows**: the installer is not signed. Windows SmartScreen warns about it: choose "
                        "More info, then Run anyway. With Smart App Control on, Windows does not run it at all.")
    if arguments.throwaway_update_key:
        warnings.append("**Updates**: this build cannot install updates by itself; download new versions from "
                        f"{arguments.repository}/releases.")
    if warnings:
        lines += ["Before installing:", ""] + [f"- {warning}" for warning in warnings] + [""]

    lines.append(f"Changes since {previous}:" if previous else "Changes:")
    lines.append("")
    lines += [f"- {subject}" for subject in subjects] or ["- No changes."]
    if previous:
        lines += ["", f"All the commits: {arguments.repository}/compare/{previous}...{tag}"]
    print("\n".join(lines))


if __name__ == "__main__":
    sys.exit(main())
