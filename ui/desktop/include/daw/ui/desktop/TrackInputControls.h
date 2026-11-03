#pragma once

#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/** "In 1", "In 1+2" or "No input". */
[[nodiscard]] juce::String describeInput(model::TrackInput input);

/** "Off", "Auto" or "On". */
[[nodiscard]] juce::String describeMonitoring(model::MonitoringMode mode);

/**
    A track's recording controls, shared by the track headers and the mixer strips:
    arm (R), input monitoring (cycles off, auto, on) and the input (a menu of the
    device's input channels, mono and stereo pairs). Edits go through the Session.
*/
class TrackInputControls
{
public:
    TrackInputControls(controller::Session& sessionToEdit, model::ObjectId trackId);

    /** Adds the three buttons to parent. */
    void addTo(juce::Component& parent);

    /** Shows the track's current settings. */
    void show(const model::Track& track);

    juce::TextButton arm{"R"};
    juce::TextButton monitoring{"Auto"};
    juce::TextButton input{"In 1"};

private:
    void showInputMenu();

    controller::Session& session;
    const model::ObjectId trackId;
};

} // namespace daw::ui::desktop
