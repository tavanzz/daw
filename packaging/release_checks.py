#!/usr/bin/env python3
"""Checks of an installed release, run by the release workflow on each platform.

    release_checks.py smoke <app> <work folder> [--plugin <.vst3>]
        Starts the installed app's smoke test: it makes a project (with the test plugin,
        through the app's own scanner), saves it, opens it again and quits; then opens a
        project from a newer format version, which must be refused. Fails on anything
        else, or if the app does not quit cleanly.

    release_checks.py update <app> <installer> <version> <tool> <key file> <work folder>
        Updates an installed copy of the app as a user would, from a local feed:
        - a valid, signed update is installed, and the new version starts and passes the
          smoke test;
        - a manifest whose signature is not valid, a file that is not the one the
          manifest describes, and an older version are all refused, the copy untouched.

<app> is what runs the app: the .app bundle (macOS), DAW.exe (Windows) or the AppImage
(Linux). <tool> is daw_update_tool, <key file> the private key the build trusts.
"""

import argparse
import hashlib
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request

SYSTEM = platform.system()
PLATFORM_KEY = {"Darwin": "macos", "Windows": "windows-x86_64", "Linux": "linux-x86_64"}[SYSTEM]
BUNDLE_ID = "io.github.tavanzz.daw"


def fail(message):
    print(f"FAILED: {message}", file=sys.stderr)
    sys.exit(1)


def executable_of(app: pathlib.Path) -> pathlib.Path:
    if SYSTEM == "Darwin":
        return app / "Contents" / "MacOS" / "DAW"
    return app


def run_app(app, arguments, environment=None, timeout=600):
    """Runs the app until it quits; returns its exit code."""
    command = [str(executable_of(app))] + arguments
    env = dict(os.environ)
    if SYSTEM == "Linux":
        env["APPIMAGE_EXTRACT_AND_RUN"] = "1"  # no FUSE on the runners
    env.update(environment or {})
    print("$", " ".join(command), flush=True)
    return subprocess.run(command, env=env, timeout=timeout).returncode


def read_json(path):
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def check_log_ended(settings: pathlib.Path):
    logs = sorted((settings / "Logs").glob("DAW *.log"))
    if not logs:
        fail("the app wrote no log")
    last = logs[-1].read_text(encoding="utf-8").strip().splitlines()[-1]
    if not last.endswith("Session ended normally."):
        fail(f"the app did not quit cleanly; its log ends with: {last}")


def smoke(app, work: pathlib.Path, plugin=None, expected_version=None):
    """The installed app makes, saves and opens a project, and refuses a newer one."""
    settings = work / "smoke-settings"
    shutil.rmtree(settings, ignore_errors=True)
    result = work / "smoke.json"
    arguments = [f"--settings-dir={settings}", "--smoke-test=new", f"--smoke-result={result}"]
    if plugin:
        arguments.append(f"--smoke-plugin={plugin}")
    code = run_app(app, arguments)
    if code != 0:
        fail(f"the smoke test ended with code {code}")
    found = read_json(result)
    print(json.dumps(found, indent=2))
    expected = {"opened": True, "saved": True, "tracks": 2, "clips": 2, "notes": 1, "unavailablePlugins": ""}
    if plugin:
        expected.update({"pluginScanned": True, "pluginReady": True})
    for key, value in expected.items():
        if found.get(key) != value:
            fail(f"smoke test: {key} is {found.get(key)!r}, expected {value!r}")
    if expected_version and found.get("version") != expected_version:
        fail(f"the app is version {found.get('version')}, expected {expected_version}")
    check_log_ended(settings)

    # A project saved by a newer version: refused with a clear message, left as it was.
    project = json.loads((settings / "Smoke" / "Smoke.dawproject").read_text(encoding="utf-8"))
    project["formatVersion"] = 999
    newer = work / "Newer.dawproject"
    newer.write_text(json.dumps(project), encoding="utf-8")
    before = newer.read_bytes()
    newer_result = work / "newer.json"
    code = run_app(app, [f"--settings-dir={settings}", f"--smoke-test={newer}", f"--smoke-result={newer_result}"])
    refused = read_json(newer_result)
    if code != 0 or refused.get("opened") or "saved by a newer version" not in refused.get("error", ""):
        fail(f"a project from a newer version was not refused as it should be: {refused}")
    if newer.read_bytes() != before:
        fail("the newer project was changed")
    print("Smoke test passed", flush=True)
    return found


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_manifest(tool, key, work, version, asset: pathlib.Path, name):
    """A signed update.json naming one file for this platform; returns its path."""
    payload = {
        "bundleId": BUNDLE_ID,
        "version": version,
        "date": time.strftime("%Y-%m-%d"),
        "notes": "A test of the updater.",
        "releaseUrl": "https://github.com/tavanzz/daw/releases",
        "assets": {PLATFORM_KEY: {"url": asset.resolve().as_uri(), "size": asset.stat().st_size,
                                  "sha256": sha256(asset)}},
    }
    payload_file = work / f"{name}-payload.json"
    payload_file.write_text(json.dumps(payload), encoding="utf-8")
    manifest = work / f"{name}.json"
    subprocess.run([str(tool), "sign", str(payload_file), str(key), str(manifest)], check=True)
    return manifest


def fingerprint(installed: pathlib.Path):
    """What an update must change in the installed copy."""
    if SYSTEM == "Darwin":
        return sha256(installed / "Contents" / "MacOS" / "DAW") + str((installed / "Contents" / "old-copy").exists())
    return sha256(installed)


def install_old_copy(app, installer, work: pathlib.Path):
    """An installed copy marked as old, as an earlier version would be."""
    old = work / "old"
    shutil.rmtree(old, ignore_errors=True)
    old.mkdir(parents=True)
    if SYSTEM == "Darwin":
        installed = old / "Applications" / "DAW.app"
        installed.parent.mkdir(parents=True)
        subprocess.run(["ditto", str(app), str(installed)], check=True)
        (installed / "Contents" / "old-copy").write_text("old")
        return installed, installed
    if SYSTEM == "Windows":
        folder = old / "DAW"
        subprocess.run([str(installer), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
                        f"/DIR={folder}"], check=True)
        installed = folder / "DAW.exe"
        with open(installed, "ab") as file:  # different bytes, still a program
            file.write(b"old copy")
        return installed, installed
    installed = old / "DAW-x86_64.AppImage"
    shutil.copy2(app, installed)
    with open(installed, "ab") as file:
        file.write(b"old copy")
    return installed, installed


def run_update(installed, manifest, settings, result, smoke_result=None, pretend="0.0.1"):
    arguments = [f"--settings-dir={settings}", f"--update-test={result}"]
    if smoke_result:
        arguments.append(f"--smoke-result={smoke_result}")
    environment = {"DAW_UPDATE_FEED": manifest.resolve().as_uri()}
    if pretend:
        environment["DAW_PRETEND_VERSION"] = pretend
    code = run_app(installed, arguments, environment)
    if code != 0:
        fail(f"the update test ended with code {code}")
    return read_json(result)


def wait_for(path: pathlib.Path, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if path.exists() and path.stat().st_size > 0:
            time.sleep(1)
            return True
        time.sleep(1)
    return False


def update(app, installer, version, tool, key, work: pathlib.Path):
    installed, runnable = install_old_copy(app, installer, work)
    settings = work / "update-settings"
    shutil.rmtree(settings, ignore_errors=True)
    old_fingerprint = fingerprint(installed)

    # Refused: a signature that is not valid.
    manifest = write_manifest(tool, key, work, version, installer, "update")
    bad = json.loads(manifest.read_text(encoding="utf-8"))
    signature = bad["signature"]
    bad["signature"] = ("B" if signature[0] != "B" else "C") + signature[1:]
    bad_signature = work / "bad-signature.json"
    bad_signature.write_text(json.dumps(bad), encoding="utf-8")
    outcome = run_update(runnable, bad_signature, settings, work / "bad-signature-result.json")
    if outcome.get("outcome") != "refused" or "signature" not in outcome.get("problem", ""):
        fail(f"an update with a bad signature was not refused: {outcome}")

    # Refused: a file other than the one the manifest describes.
    tampered = work / ("tampered-" + installer.name)
    shutil.copy2(installer, tampered)
    tampered_manifest = write_manifest(tool, key, work, version, tampered, "tampered")
    with open(tampered, "r+b") as file:  # same size, one byte changed
        file.seek(tampered.stat().st_size // 2)
        byte = file.read(1)
        file.seek(-1, os.SEEK_CUR)
        file.write(bytes([byte[0] ^ 0xFF]))
    outcome = run_update(runnable, tampered_manifest, settings, work / "tampered-result.json")
    if outcome.get("outcome") != "refused" or "SHA-256" not in outcome.get("problem", ""):
        fail(f"a tampered update was not refused: {outcome}")

    # Not offered: an older version (never a downgrade).
    older = write_manifest(tool, key, work, "0.0.1", installer, "older")
    outcome = run_update(runnable, older, settings, work / "older-result.json", pretend=None)
    if outcome.get("outcome") != "upToDate":
        fail(f"an older version was offered: {outcome}")

    if fingerprint(installed) != old_fingerprint:
        fail("a refused update changed the installed copy")
    print("Bad signature, tampered file and downgrade refused", flush=True)

    # Installed: the valid update; the new version starts and runs the smoke test.
    smoke_result = work / "after-update.json"
    smoke_result.unlink(missing_ok=True)
    outcome = run_update(runnable, manifest, settings, work / "update-result.json", smoke_result)
    if outcome.get("outcome") != "installing" or outcome.get("found") != version:
        fail(f"the valid update was not installed: {outcome}")
    if not wait_for(smoke_result, 300):
        fail("the new version did not start (no smoke test result)")
    after = read_json(smoke_result)
    print(json.dumps(after, indent=2))
    if not after.get("opened") or after.get("version") != version:
        fail(f"the updated app did not pass its smoke test: {after}")
    # The helper finishes after the app quits: give it a moment, then compare.
    time.sleep(5)
    if fingerprint(installed) == old_fingerprint:
        fail("the installed copy was not replaced")
    print("Update installed and started", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    smoke_parser = commands.add_parser("smoke")
    smoke_parser.add_argument("app", type=pathlib.Path)
    smoke_parser.add_argument("work", type=pathlib.Path)
    smoke_parser.add_argument("--plugin", type=pathlib.Path)
    smoke_parser.add_argument("--version")
    update_parser = commands.add_parser("update")
    for name in ("app", "installer"):
        update_parser.add_argument(name, type=pathlib.Path)
    update_parser.add_argument("version")
    for name in ("tool", "key", "work"):
        update_parser.add_argument(name, type=pathlib.Path)
    arguments = parser.parse_args()

    arguments.work.mkdir(parents=True, exist_ok=True)
    if arguments.command == "smoke":
        smoke(arguments.app, arguments.work.resolve(), arguments.plugin, arguments.version)
    else:
        update(arguments.app, arguments.installer.resolve(), arguments.version, arguments.tool, arguments.key,
               arguments.work.resolve())


if __name__ == "__main__":
    main()
