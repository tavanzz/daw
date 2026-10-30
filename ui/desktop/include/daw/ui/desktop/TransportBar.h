#pragma once

#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace daw::ui::desktop
{

/** Transport buttons, the playhead in time and in bars, tempo and metre (double-click
    to edit), snap and grid. Everything goes through the Session. */
class TransportBar final : public juce::Component
{
public:
    explicit TransportBar(controller::Session& sessionToControl);

    /** Shows the session's current state; call it from the UI's refresh timer. */
    void refresh();

    void resized() override;

private:
    void showTempoAndMetre();

    controller::Session& session;

    juce::TextButton playButton{"Play"};
    juce::TextButton pauseButton{"Pause"};
    juce::TextButton stopButton{"Stop"};
    juce::TextButton loopButton{"Loop"};
    juce::Label timeLabel;
    juce::Label barsLabel;
    juce::Label tempoLabel;
    juce::Label metreLabel;
    juce::TextButton snapButton{"Snap"};
    juce::ComboBox gridBox;

    juce::uint64 shownRevision = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace daw::ui::desktop
