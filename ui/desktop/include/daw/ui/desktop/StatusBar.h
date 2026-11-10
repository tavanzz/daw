#pragma once

#include "daw/controller/CoopSession.h"
#include "daw/controller/Session.h"
#include "daw/ui/desktop/AnimationLink.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace daw::ui::desktop
{

/** The line along the bottom of the window: how a recording goes (count-in, disk time
    left), passing warnings such as dropouts, and the device's rate and the recording
    format. Acid Terminal draws it as a shell prompt. */
class StatusBar final : public juce::Component
{
public:
    static constexpr int height = 24;

    explicit StatusBar(controller::Session& sessionToShow);

    /** Shows the session's current state; call it from the UI's refresh timer. */
    void refresh();

    /** Shows a warning for a few seconds. */
    void showWarning(const juce::String& text);

    /** Shows the state of this co-op session too (null: none). */
    void setCoop(const controller::CoopSession* coopToShow);

    [[nodiscard]] const ThemedLookAndFeel::StatusLine& getLine() const noexcept { return line; }

    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

private:
    controller::Session& session;
    const controller::CoopSession* coop = nullptr;
    ThemedLookAndFeel::StatusLine line;
    juce::String warning;
    juce::uint32 warningUntil = 0;

    // The prompt's cursor blinks (Acid Terminal), with the cursor glow effect.
    bool cursorOn = true;
    AnimationLink cursor{*this, appearance::AnimationEffect::cursorGlow, [this](double seconds)
                         {
                             const bool on = std::fmod(seconds, 1.06) < 0.53;
                             if (on != cursorOn)
                             {
                                 cursorOn = on;
                                 repaint();
                             }
                         }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

} // namespace daw::ui::desktop
