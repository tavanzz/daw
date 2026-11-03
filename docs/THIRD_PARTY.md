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
to `font_licences` in `app/CMakeLists.txt`, and add a row here.
