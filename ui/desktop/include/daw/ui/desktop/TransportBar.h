#pragma once

#include "daw/engine/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace daw::ui::desktop
{

/** Play, pause, stop and loop buttons, and the position display. */
class TransportBar final : public juce::Component
{
public:
    TransportBar();

    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void(bool)> onLoopToggled;

    /** Shows the engine's state; call it from the UI's refresh timer. */
    void update(engine::TransportState state, juce::int64 position, juce::int64 length,
                double sampleRate, bool looping, bool hasAudio);

    void resized() override;

private:
    juce::TextButton playButton{"Play"};
    juce::TextButton pauseButton{"Pause"};
    juce::TextButton stopButton{"Stop"};
    juce::TextButton loopButton{"Loop"};
    juce::Label timeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

/** "m:ss.mmm", the way the transport shows positions. */
[[nodiscard]] juce::String formatTime(double seconds);

} // namespace daw::ui::desktop
