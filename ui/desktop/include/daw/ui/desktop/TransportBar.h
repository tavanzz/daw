#pragma once

#include "daw/controller/Session.h"
#include "daw/ui/desktop/Logo.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace daw::ui::desktop
{

/** The logo, transport buttons, the playhead in time and in bars, tempo and metre
    (double-click to edit), snap and grid; record, metronome, count-in and punch.
    Everything goes through the Session; how a recording goes is in the StatusBar. */
class TransportBar final : public juce::Component
{
public:
    explicit TransportBar(controller::Session& sessionToControl);

    /** Shows the session's current state; call it from the UI's refresh timer. */
    void refresh();

    /** What the record button does (it may ask to save the project first). */
    std::function<void()> onRecord;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int height = 44;

private:
    /** A toggle with a menu of options on right-click. */
    class OptionButton final : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void()> onRightClick;

        void mouseDown(const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu() && onRightClick)
                onRightClick();
            else
                juce::TextButton::mouseDown(event);
        }
    };

    void showTempoAndMetre();

    controller::Session& session;

    Logo logo;
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

    juce::TextButton recordButton{"Rec"};
    OptionButton metronomeButton{"Click"};
    OptionButton countInButton{"Count"};
    juce::TextButton punchButton{"Punch"};

    juce::uint64 shownRevision = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace daw::ui::desktop
