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

    release_checks.py previous <installer> <version> <tool> <key file> <work folder>
        Installs the newest release published before this one (from GitHub, with gh), lets
        it update itself to this build from a local feed as a user's copy would, and checks
        that the new version starts under its own name (also when the old one had another),
        takes over the old copy's settings and passes the smoke test.

<app> is what runs the app: the .app bundle (macOS), MICINSTATION.exe (Windows) or the
AppImage (Linux). <tool> is daw_update_tool, <key file> the private key the build trusts.
"""

import argparse
import hashlib
import json
import os
import pathlib
import platform
import plistlib
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request

SYSTEM = platform.system()
PLATFORM_KEY = {"Darwin": "macos", "Windows": "windows-x86_64", "Linux": "linux-x86_64"}[SYSTEM]
NAME = "MICINSTATION"
PREVIOUS_NAME = "DAW"  # the app's name up to v0.8.0-rc.4
# The app update manifests name (BuildInfo::updateId): its first bundle id, for ever.
UPDATE_ID = "io.github.tavanzz.daw"
SUFFIX = {"Darwin": "-macOS.dmg", "Windows": "-Windows-x64-Setup.exe", "Linux": "-Linux-x86_64.AppImage"}[SYSTEM]


def fail(message):
    print(f"FAILED: {message}", file=sys.stderr)
    sys.exit(1)


def executable_of(app: pathlib.Path) -> pathlib.Path:
    if SYSTEM == "Darwin":
        with open(app / "Contents" / "Info.plist", "rb") as file:
            return app / "Contents" / "MacOS" / plistlib.load(file)["CFBundleExecutable"]
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
    logs = sorted((settings / "Logs").glob(f"{NAME} *.log"))
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

    # A project saved before the app was renamed, with the old extension: it opens.
    saved = settings / "Smoke" / "Smoke.micin"
    old = settings / "Smoke" / "Old.dawproject"
    shutil.copy2(saved, old)
    old_result = work / "old-extension.json"
    code = run_app(app, [f"--settings-dir={settings}", f"--smoke-test={old}", f"--smoke-result={old_result}"])
    opened = read_json(old_result)
    if code != 0 or not opened.get("opened") or opened.get("tracks") != 2:
        fail(f"a .dawproject project did not open: {opened}")

    # A project saved by a newer version: refused with a clear message, left as it was.
    project = json.loads(saved.read_text(encoding="utf-8"))
    project["formatVersion"] = 999
    newer = work / "Newer.micin"
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
        "bundleId": UPDATE_ID,
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
        return sha256(executable_of(installed)) + str((installed / "Contents" / "old-copy").exists())
    return sha256(installed)


def install_old_copy(app, installer, work: pathlib.Path):
    """An installed copy marked as old, as an earlier version would be."""
    old = work / "old"
    shutil.rmtree(old, ignore_errors=True)
    old.mkdir(parents=True)
    if SYSTEM == "Darwin":
        installed = old / "Applications" / f"{NAME}.app"
        installed.parent.mkdir(parents=True)
        subprocess.run(["ditto", str(app), str(installed)], check=True)
        (installed / "Contents" / "old-copy").write_text("old")
        return installed, installed
    if SYSTEM == "Windows":
        folder = old / NAME
        subprocess.run([str(installer), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
                        f"/DIR={folder}"], check=True)
        installed = folder / f"{NAME}.exe"
        with open(installed, "ab") as file:  # different bytes, still a program
            file.write(b"old copy")
        return installed, installed
    installed = old / f"{NAME}-x86_64.AppImage"
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


def download_previous_release(work: pathlib.Path):
    """The newest published release's file for this platform, and its version."""
    listed = json.loads(subprocess.run(["gh", "release", "list", "--limit", "30", "--json",
                                        "tagName,isDraft,publishedAt"], check=True, capture_output=True,
                                       text=True).stdout)
    releases = sorted((release for release in listed if not release["isDraft"] and release["publishedAt"]),
                      key=lambda release: release["publishedAt"], reverse=True)
    if not releases:
        fail("no release was published before this one")
    tag = releases[0]["tagName"]
    folder = work / "download"
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    subprocess.run(["gh", "release", "download", tag, "--pattern", f"*{SUFFIX}", "--dir", str(folder)], check=True)
    files = list(folder.glob(f"*{SUFFIX}"))
    if len(files) != 1:
        fail(f"{tag} has no single file *{SUFFIX}")
    print(f"Previous release: {tag}, {files[0].name}", flush=True)
    return files[0], tag[1:]


def install_previous(installer: pathlib.Path, work: pathlib.Path):
    """Installs a release as a user would; returns what runs it and where it is installed."""
    folder = work / "installed"
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    if SYSTEM == "Darwin":
        mount = work / "mount"
        mount.mkdir(exist_ok=True)
        subprocess.run(["hdiutil", "attach", "-nobrowse", "-readonly", "-mountpoint", str(mount), str(installer)],
                       check=True)
        apps = list(mount.glob("*.app"))
        installed = folder / "Applications" / apps[0].name
        installed.parent.mkdir()
        subprocess.run(["ditto", str(apps[0]), str(installed)], check=True)
        subprocess.run(["hdiutil", "detach", str(mount)], check=True)
        return installed, folder / "Applications"
    if SYSTEM == "Windows":
        target = folder / PREVIOUS_NAME
        subprocess.run([str(installer), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
                        f"/DIR={target}"], check=True)
        programs = [program for program in target.glob("*.exe")
                    if not program.name.startswith("unins") and "Plugin Scanner" not in program.name]
        return programs[0], target
    installed = folder / installer.name
    shutil.copy2(installer, installed)
    installed.chmod(0o755)
    return installed, folder


def semantic_order(version):
    """A key that sorts versions as Semantic Versioning 2.0.0 does (services::SemanticVersion)."""
    core, _, pre = version.partition("-")
    numbers = tuple(int(part) for part in core.split("."))
    if not pre:
        return numbers, (1,)
    identifiers = tuple((0, int(part), "") if part.isdigit() else (1, 0, part) for part in pre.split("."))
    return numbers, (0, identifiers)


def previous(installer, version, tool, key, work: pathlib.Path):
    old_installer, old_version = download_previous_release(work)
    runnable, location = install_previous(old_installer, work)
    print(f"Installed {old_version}: {runnable}", flush=True)
    settings = work / "settings"
    shutil.rmtree(settings, ignore_errors=True)

    # A run by hand builds 0.8.0-dev.N, which comes before a release candidate: the
    # manifest then offers a version just above the old one, and the build is installed.
    offered = version
    if semantic_order(version) <= semantic_order(old_version):
        offered = old_version + ".1"
        print(f"{version} is not newer than {old_version}: offered as {offered}", flush=True)
    manifest = write_manifest(tool, key, work, offered, installer, "update")
    smoke_result = work / "after-update.json"
    smoke_result.unlink(missing_ok=True)
    outcome = run_update(runnable, manifest, settings, work / "update-result.json", smoke_result, pretend=None)
    if outcome.get("outcome") != "installing" or outcome.get("found") != offered:
        fail(f"{old_version} did not install the update: {outcome}")
    if not wait_for(smoke_result, 300):
        fail("the new version did not start (no smoke test result)")
    after = read_json(smoke_result)
    print(json.dumps(after, indent=2))
    if not after.get("opened") or after.get("version") != version:
        fail(f"the updated app did not pass its smoke test: {after}")
    time.sleep(5)

    # Under its own name, wherever the old copy was.
    if SYSTEM == "Darwin":
        names = sorted(path.name for path in location.iterdir())
        if names != [f"{NAME}.app"]:
            fail(f"the Applications folder holds {names}, expected only {NAME}.app")
        app = location / f"{NAME}.app"
    elif SYSTEM == "Windows":
        app = location / f"{NAME}.exe"
        if not app.exists() or (location / f"{PREVIOUS_NAME} Plugin Scanner.exe").exists():
            fail(f"the installation holds {sorted(path.name for path in location.iterdir())}")
        shortcuts = pathlib.Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs"
        if not (shortcuts / f"{NAME}.lnk").exists() or (shortcuts / f"{PREVIOUS_NAME}.lnk").exists():
            fail(f"the Start menu holds {sorted(path.name for path in shortcuts.glob('*.lnk'))}")
    else:
        app = runnable
    described = subprocess.run([str(executable_of(app)), "--version"], capture_output=True, text=True,
                               env=dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1"), timeout=120).stdout
    if not described.startswith(f"{NAME} {version} "):
        fail(f"the installed app says: {described!r}")

    # The old copy's settings, taken over.
    if not (settings / f"{NAME}.settings").exists():
        fail("the old copy's settings were not taken over")
    log = "".join(path.read_text(encoding="utf-8") for path in (settings / "Logs").glob(f"{NAME} *.log"))
    if PREVIOUS_NAME != NAME and "Took over the settings of" not in log:
        fail("the log does not say what was taken over")

    if SYSTEM == "Windows":
        subprocess.run([str(location / "unins000.exe"), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], check=True)
        time.sleep(5)
        left = sorted(path.name for path in location.glob("*.exe"))
        if left:
            fail(f"the uninstaller left {left}")
    print(f"{old_version} updated itself to {version}", flush=True)


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
    previous_parser = commands.add_parser("previous")
    previous_parser.add_argument("installer", type=pathlib.Path)
    previous_parser.add_argument("version")
    for name in ("tool", "key", "work"):
        previous_parser.add_argument(name, type=pathlib.Path)
    arguments = parser.parse_args()

    arguments.work.mkdir(parents=True, exist_ok=True)
    if arguments.command == "smoke":
        smoke(arguments.app, arguments.work.resolve(), arguments.plugin, arguments.version)
    elif arguments.command == "previous":
        previous(arguments.installer.resolve(), arguments.version, arguments.tool, arguments.key,
                 arguments.work.resolve())
    else:
        update(arguments.app, arguments.installer.resolve(), arguments.version, arguments.tool, arguments.key,
               arguments.work.resolve())


if __name__ == "__main__":
    main()
