#!/usr/bin/env python3
"""Writes the payload of a release's update.json (services/UpdateManifest.h): the version,
the date, the notes, and for each platform its file's address, size and SHA-256.

    tools/make_update_payload.py <version> <tag> <notes.md> <folder with the release files>

The release workflow then signs it with daw_update_tool. Printed on standard output.
"""

import datetime
import hashlib
import json
import pathlib
import sys

REPOSITORY = "https://github.com/tavanzz/daw"
# The app the manifest is for (BuildInfo::updateId): the app's first bundle id, never changed,
# since copies from before it was renamed MICINSTATION accept nothing else.
UPDATE_ID = "io.github.tavanzz.daw"
PLATFORMS = {"macos": "-macOS.dmg", "windows-x86_64": "-Windows-x64-Setup.exe", "linux-x86_64": "-Linux-x86_64.AppImage"}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    version, tag, notes, folder = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3]), pathlib.Path(sys.argv[4])
    assets = {}
    for platform, ending in PLATFORMS.items():
        files = sorted(folder.glob(f"*{ending}"))
        if len(files) != 1:
            sys.exit(f"expected one *{ending} in {folder}, found {len(files)}")
        file = files[0]
        assets[platform] = {"url": f"{REPOSITORY}/releases/download/{tag}/{file.name}",
                            "size": file.stat().st_size, "sha256": sha256(file)}
    payload = {"bundleId": UPDATE_ID, "version": version, "date": datetime.date.today().isoformat(),
               "notes": notes.read_text(encoding="utf-8").strip(),
               "releaseUrl": f"{REPOSITORY}/releases/tag/{tag}", "assets": assets}
    print(json.dumps(payload, indent=2))


if __name__ == "__main__":
    main()
