# Third-party components

What the app and its build use that this project did not write, and under which
licence. This file is copied into the app (macOS: `DAW.app/Contents/Resources/Licences`;
Windows and Linux: the `Licences` folder next to the executable), together with the
app's own licence (`LICENSE.txt`) and the licence text of each component; Help > About
shows them all. The app's own code is not open source (`LICENSE.txt`).

## Libraries

| Component | Version | Licence | Used for |
|-----------|---------|---------|----------|
| [JUCE](https://juce.com) | 9.0.4 | used under the commercial [JUCE 9 licence](https://juce.com/legal/juce-9-licence/), Starter tier (it is also available under the AGPLv3, which this project does not use) | audio, graphics, the whole app; on macOS with one change of ours to its CoreAudio code (`cmake/patches/`) |
| [Monocypher](https://monocypher.org) | 4.0.2 | CC0-1.0 or BSD 2-Clause (either) | checking the Ed25519 signatures of updates |
| [sentry-native](https://github.com/getsentry/sentry-native) | 0.17.4 | MIT | crash reports (its in-process backend; nothing is sent without consent) |
| ↳ [mpack](https://github.com/ludocode/mpack) | as sentry-native ships it | MIT | inside sentry-native |
| ↳ [jsmn](https://github.com/zserge/jsmn) | as sentry-native ships it | MIT | inside sentry-native |
| ↳ [stb_sprintf](https://github.com/nothings/stb) | as sentry-native ships it | public domain or MIT | inside sentry-native |
| ↳ [libunwind](https://www.nongnu.org/libunwind/) | as sentry-native ships it | MIT | inside sentry-native, Linux only |
| [Catch2](https://github.com/catchorg/Catch2) | 3.16.1 | Boost Software License 1.0 | unit tests only, not in the app |

They are downloaded when the build is configured, at pinned versions verified by
SHA-256 (`cmake/Dependencies.cmake`). JUCE's Starter licence is free while revenue or
funding stays under US$20,000 in twelve months; above that a paid JUCE licence (Indie or
Pro) is needed. Because JUCE is not used under the AGPLv3, nothing under the GPL, AGPL
or LGPL is linked into the app (the JUCE 9 EULA, section 2.3).

## Packaging tools and what they put in the installers

| Component | Version | Licence | Where |
|-----------|---------|---------|-------|
| [Inno Setup](https://jrsoftware.org/isinfo.php) | 6.7.3 | Inno Setup licence (free, commercial use allowed) | builds the Windows installer; its setup and uninstall code is inside the installer |
| [AppImage type-2 runtime](https://github.com/AppImage/type2-runtime) | 20251108 | MIT; statically includes libfuse (LGPL 2.1) and squashfuse (BSD), a separate program from the app | the start of the Linux AppImage, which mounts it; sources at the link |
| [appimagetool](https://github.com/AppImage/appimagetool) | 1.9.1 | MIT | builds the AppImage (not shipped) |
| [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) | 1-alpha-20251107-1 | MIT | gathers the AppImage's libraries (not shipped) |

All are downloaded by the release workflow at pinned versions checked by SHA-256.

## Plugin SDKs

Plugin hosting (Phase 7) compiles parts of these SDKs into the app. They come inside
JUCE's source tree (`modules/juce_audio_processors_headless/format_types/`), so they
are pinned and verified with JUCE. Their licences, checked on 2026-11-05 in the files
JUCE ships, travel with the app in `Licences/<component>/`.

| Component | Version | Licence | Used for |
|-----------|---------|---------|----------|
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) (Steinberg Media Technologies) | 3.8.0 (`VST3_SDK/`, two files changed by JUCE, marked) | MIT (Steinberg relicensed the SDK under MIT in 2025; earlier versions were GPLv3 or Steinberg's proprietary licence) | hosting VST3 plugins, all platforms; also the test plugins |
| [lilv](https://drobilla.net/software/lilv.html) | 0.24.12 | ISC | hosting LV2 plugins, Linux only |
| [serd](https://drobilla.net/software/serd.html) | 0.30.10 | ISC | LV2 (reading Turtle files), Linux only |
| [sord](https://drobilla.net/software/sord.html) | 0.16.9 | ISC | LV2 (RDF store), Linux only |
| [sratom](https://drobilla.net/software/sratom.html) | 0.6.8 | ISC | LV2 (atoms), Linux only |
| [LV2](https://lv2plug.in) headers | as JUCE ships them | ISC | LV2, Linux only |
| [Audio Unit SDK](https://github.com/apple/AudioUnitSDK) (Apple) | as JUCE ships it (`juce_audio_plugin_client/AU/AudioUnitSDK/`) | Apache License 2.0 | the Audio Unit test plugins only, not in the app |

Hosting Audio Units uses macOS's own frameworks (AudioToolbox, CoreAudioKit): no SDK
is compiled in. "VST" is a trademark of Steinberg Media Technologies GmbH: the name
and logo are governed by Steinberg's usage guidelines (`VST3_Usage_Guidelines.pdf`,
shipped with the SDK), not by the MIT licence; the app shows no VST logo. CLAP (MIT)
is not hosted yet: JUCE 9.0.4 has no CLAP support.

## Fonts

The themes' fonts are built into the app (`appearance/fonts/`, embedded with
`juce_add_binary_data`); nothing is downloaded at run time. All of them are licensed
under the [SIL Open Font License 1.1](https://openfontlicense.org): they may be used,
bundled and redistributed with software, sold or not, as long as they are not sold
by themselves and their licence goes with them. The full licence of each family is
the `OFL.txt` next to its files.

| Family | Styles bundled | Version | Copyright | Source |
|--------|----------------|---------|-----------|--------|
| Chakra Petch | Regular, SemiBold | 1.000 | 2018 The Chakra Petch Project Authors | [google/fonts](https://github.com/google/fonts/tree/main/ofl/chakrapetch) |
| JetBrains Mono | Regular, Bold | 2.305 | 2020 The JetBrains Mono Project Authors | [JetBrains/JetBrainsMono](https://github.com/JetBrains/JetBrainsMono/tree/master/fonts/ttf) |
| Space Grotesk | Medium, Bold | 2.000 | 2020 The Space Grotesk Project Authors | [floriankarsten/space-grotesk](https://github.com/floriankarsten/space-grotesk/tree/master/fonts/ttf/static) |
| IBM Plex Mono | Regular, SemiBold | 2.3 | 2017 IBM Corp., Reserved Font Name "Plex" | [google/fonts](https://github.com/google/fonts/tree/main/ofl/ibmplexmono) |
| Michroma | Regular | 1.100 | 2011 The Michroma Project Authors | [google/fonts](https://github.com/google/fonts/tree/main/ofl/michroma) |
| Rajdhani | Medium, SemiBold | 1.201 | 2014 Indian Type Foundry | [google/fonts](https://github.com/google/fonts/tree/main/ofl/rajdhani) |

The files are the upstream static TrueType files, unmodified (downloaded on
2026-11-03). JetBrains Mono and Space Grotesk come from their own repositories
because Google Fonts ships them only as variable fonts. A modified font may not keep
a Reserved Font Name such as "Plex"; these are not modified.

To add a font: put its files and `OFL.txt` in `appearance/fonts/<Family>/`, list them
in `appearance/CMakeLists.txt` and `appearance/src/BundledFonts.cpp`, add the family
to the font list of `licences` in `app/CMakeLists.txt`, and add a row here.
