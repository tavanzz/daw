# Releasing

How a version of MICINSTATION reaches friends: a tag makes GitHub Actions build, sign, package,
check and publish it (`.github/workflows/release.yml`). Every account below is optional:
without one, its part is skipped, the builds say so in their notes, and the workflow
stays green.

## Making a release

1. Set the version in the top-level `CMakeLists.txt` (`project(daw VERSION X.Y.Z ...)`)
   and commit it on `main`.
2. Tag and push:

   ```bash
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```

   A tag `vX.Y.Z-rc.N` makes a pre-release instead: the same files, which only copies
   with Help > Include Pre-releases are offered.
3. The workflow takes about an hour. It refuses a tag that disagrees with
   `CMakeLists.txt`. When it is green the release is on
   https://github.com/tavanzz/daw/releases with:
   - `MICINSTATION-X.Y.Z-macOS.dmg` (Apple Silicon and Intel),
     `MICINSTATION-X.Y.Z-Windows-x64-Setup.exe`, `MICINSTATION-X.Y.Z-Linux-x86_64.AppImage`
     (up to v0.8.0-rc.4, when the app was called DAW: `DAW-...`);
   - `update.json`, the signed update information every installed copy reads;
   - `SHA256SUMS.txt`, and the debug symbols of each platform (`*-symbols.zip`).
4. The notes are the subjects of the commits since the previous release (merges and
   "CI:" commits left out). Edit them on GitHub if you want; the update panel shows the
   text in `update.json`, written when the release was made.

Actions > Release > Run workflow builds and checks everything without publishing (a trial
run).

## What the workflow checks

On each platform, before publishing: the tests; the signatures (and on macOS
notarisation, stapled tickets and Gatekeeper on a quarantined copy); the installed app
starts, makes a project with the test plugin through its own scanner, opens it again
(also as a `.dawproject`), refuses a project from a newer version and quits cleanly; on
Windows for one user and for everyone, then uninstalls; an update end to end: an
installed copy pretending to be older takes the new version from a local feed, starts it
and passes the same check, while a bad signature, a tampered file and an older version
are refused; and, when the update key is there (the copies already published trust no
other), an update from the previous release: the newest one published is downloaded,
installed as a user would, and updates itself to the new build, which must start under
its own name (also from a copy still called DAW), take over the old copy's settings and
pass the check. After publishing, it reads `update.json` from GitHub as the app does and
checks its signature.

## Accounts and secrets

All go in GitHub: the repository's Settings > Secrets and variables > Actions. Secrets
on the Secrets tab, variables on the Variables tab, with exactly these names.

### 1. The update key (free, no account; do this first)

Installed copies only accept updates signed with this key: without it each run signs
with a throwaway key, and its builds can never update themselves.

```bash
cmake --build --preset debug --target daw_update_tool
build/debug/tools/daw_update_tool keygen ~/daw-update-key.txt
```

It prints the public key and writes the private one to `~/daw-update-key.txt`
(readable by you only).

| Where | Name | Value |
|---|---|---|
| Secret | `DAW_UPDATE_PRIVATE_KEY` | the contents of `~/daw-update-key.txt` |
| Variable | `DAW_UPDATE_PUBLIC_KEY` | the public key it printed |

Keep a copy of `~/daw-update-key.txt` somewhere safe and offline (a password manager):
if it is lost, the copies already installed cannot be updated anymore and friends must
reinstall by hand once. Never commit it.

### 2. Apple (macOS signing and notarisation): 99 USD a year (98,99 € in Italy)

1. Enrol in the Apple Developer Program as an individual at
   https://developer.apple.com/programs/enroll/ (an Apple Account with two-factor
   authentication, your legal name, which friends will see as the developer, and your
   address). Approval can take a few days.
2. Make a Developer ID Application certificate: on the Mac, Keychain Access >
   Certificate Assistant > Request a Certificate From a Certificate Authority (saved to
   disk); then https://developer.apple.com/account/resources/certificates/add, choose
   "Developer ID Application", upload the request, download the certificate and
   double-click it. In Keychain Access, under My Certificates, right-click "Developer ID
   Application: <your name>" > Export, as `.p12`, with a password.
3. Make an App Store Connect API key for notarisation:
   https://appstoreconnect.apple.com/access/integrations/api > Team Keys > +, access
   "Developer". Download the `.p8` (only once), and note the Key ID and the Issuer ID
   shown on that page.

| Where | Name | Value |
|---|---|---|
| Secret | `MACOS_CERTIFICATE_P12` | `base64 -i certificate.p12 \| pbcopy`, pasted |
| Secret | `MACOS_CERTIFICATE_PASSWORD` | the `.p12`'s password |
| Secret | `MACOS_NOTARY_KEY_P8` | the text of the `.p8` file (with its BEGIN and END lines) |
| Secret | `MACOS_NOTARY_KEY_ID` | the Key ID |
| Secret | `MACOS_NOTARY_ISSUER_ID` | the Issuer ID |

Developer ID certificates now last one year: renew it before it expires (Apple writes)
and replace the two certificate secrets. Apps already notarised keep working.

### 3. Sentry (crash reports): free

1. Sign up at https://sentry.io and, when creating the organisation, choose the data
   storage location **European Union**. It cannot be changed later.
2. Create a project for the platform **Native**; Settings > Projects > (it) > Client Keys
   shows its DSN, `https://<key>@o<number>.ingest.de.sentry.io/<number>`.
3. In the project's Settings > Security & Privacy, switch on "Prevent Storing of IP
   Addresses" and leave the data scrubber on.
4. Settings > Developer Settings > Organization Tokens > Create New Token (it allows
   uploading debug files).

| Where | Name | Value |
|---|---|---|
| Variable | `DAW_SENTRY_DSN` | the DSN |
| Variable | `SENTRY_ORG` | the organisation's slug (in its URL) |
| Variable | `SENTRY_PROJECT` | the project's slug |
| Secret | `SENTRY_AUTH_TOKEN` | the token |

Builds made with a DSN ask at their first launch whether crash reports may be sent;
builds without one never send any (`docs/PRIVACY.md`).

### 4. Windows code signing (later): about 129 + 180 USD a year

Not set up yet: the installer is unsigned, and SmartScreen asks friends to confirm. When
wanted: an individual (IV) code signing certificate from SSL.com with eSigner cloud
signing, which the workflow already uses (`sslcom/esigner-codesign`). On SSL.com, enrol
the certificate in eSigner and note the credential ID; the TOTP secret is shown when
eSigner is set up (keep it, it cannot be shown again).

| Where | Name | Value |
|---|---|---|
| Secret | `SSLCOM_USERNAME` | the SSL.com account's user name |
| Secret | `SSLCOM_PASSWORD` | its password |
| Secret | `SSLCOM_CREDENTIAL_ID` | the eSigner credential ID |
| Secret | `SSLCOM_TOTP_SECRET` | the eSigner TOTP secret |

A signed installer shows your name instead of "Unknown publisher"; SmartScreen may still
warn until enough people have installed it. (Azure Artifact Signing costs less, but is
open to individuals only in the United States and Canada; to companies in the EU.)

## Locally

The same steps run on a Mac without any account: `cmake --preset macos-universal`, build,
`packaging/macos/sign.sh <MICINSTATION.app> -`, `packaging/macos/make-dmg.sh`,
`packaging/macos/verify.sh <dmg> unsigned`, then `packaging/release_checks.py smoke` and
`packaging/release_checks.py update` (with a key made by `daw_update_tool keygen` and
passed to CMake as `DAW_UPDATE_PUBLIC_KEY`).
