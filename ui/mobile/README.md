# ui/mobile

Touch interface for iOS/iPadOS and Android. Intentionally empty for now: it is
not part of the build and has no code.

When it is started it follows the same rules as `ui/desktop`:

- a static library (`daw_ui_mobile`) that depends only on `engine`, `model` and
  JUCE GUI modules, never the other way round;
- public headers in `include/daw/ui/mobile/`, code in `src/`, namespace
  `daw::ui::mobile`;
- `app/` chooses the desktop or mobile interface depending on the platform.

Anything both interfaces need (waveform peaks, meter values, transport state)
belongs in `engine` or `model`, not in either UI.
