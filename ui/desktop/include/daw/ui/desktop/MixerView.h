#pragma once

#include "daw/controller/CoopSession.h"
#include "daw/controller/Session.h"
#include "daw/ui/desktop/LevelMeter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/**
    The mixer: a strip per track (inserts, sends, fader, pan, mute, solo, meter),
    then one per aux bus (inserts, fader, pan, mute, meter; double-click the name to
    rename it, right-click to delete it), and the master strip (inserts after its
    fader). Faders show dB, from −∞ to +6; double-click a fader or the pan knob to
    reset it. A fader or knob drag is one undo step. Automated faders and knobs
    follow their automation and cannot be dragged.
*/
class MixerView final : public juce::Component
{
public:
    static constexpr int preferredHeight = 470;

    explicit MixerView(controller::Session& sessionToEdit);
    ~MixerView() override;

    /** Opens an effect's editor (clicked in a strip). */
    std::function<void(model::ObjectId effectId)> openEffect;

    /** Call from the UI's refresh timer with the master output peaks (the meter
        reader takes them, so they are read in one place) and the time passed. */
    void refresh(float masterLeftPeak, float masterRightPeak, double elapsedSeconds);

    /** The co-op session whose locks the strips show (null: none). */
    void setCoop(const controller::CoopSession* coopToShow) noexcept { coop = coopToShow; }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Strip;

    void rebuildStrips(bool projectChanged);

    controller::Session& session;
    juce::Viewport stripViewport;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<Strip>> strips;
    std::unique_ptr<Strip> masterStrip;
    juce::uint64 shownRevision = 0;
    const controller::CoopSession* coop = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

/** Fader positions in dB: the bottom of the fader is silence. */
[[nodiscard]] double faderDbToGain(double decibels) noexcept;
[[nodiscard]] double gainToFaderDb(double gain) noexcept;

} // namespace daw::ui::desktop
