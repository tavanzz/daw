# Third-party components

What the app and its build use that this project did not write, and under which
licence. This file is copied into the app (macOS: `DAW.app/Contents/Resources/Licences`;
Windows and Linux: the `Licences` folder next to the executable), together with the
font licences.

## Libraries

| Component | Version | Licence | Used for |
|-----------|---------|---------|----------|
| [JUCE](https://juce.com) | 9.0.4 | AGPLv3 or the commercial JUCE 9 licence | audio, graphics, the whole app (see the note at the top of the README) |
| [Catch2](https://github.com/catchorg/Catch2) | 3.16.1 | Boost Software License 1.0 | unit tests only, not in the app |

Both are downloaded when the build is configured, at pinned versions verified by
SHA-256 (`cmake/Dependencies.cmake`).

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
