#pragma once

#include "daw/controller/Session.h"
#include "daw/ui/desktop/LevelMeter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/**
    The mixer: a strip per track (fader, pan, mute, solo, meter) and the master
    strip. Faders show dB, from −∞ to +6; double-click a fader or the pan knob to
    reset it. A fader or knob drag is one undo step.
*/
class MixerView final : public juce::Component
{
public:
    static constexpr int preferredHeight = 270;

    explicit MixerView(controller::Session& sessionToEdit);
    ~MixerView() override;

    /** Call from the UI's refresh timer with the master output peaks (the meter
        reader takes them, so they are read in one place) and the time passed. */
    void refresh(float masterLeftPeak, float masterRightPeak, double elapsedSeconds);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Strip;

    void rebuildStrips();

    controller::Session& session;
    juce::Viewport stripViewport;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<Strip>> strips;
    std::unique_ptr<Strip> masterStrip;
    juce::uint64 shownRevision = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

/** Fader positions in dB: the bottom of the fader is silence. */
[[nodiscard]] double faderDbToGain(double decibels) noexcept;
[[nodiscard]] double gainToFaderDb(double gain) noexcept;

} // namespace daw::ui::desktop
