#pragma once

#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/** "In 1", "In 1+2" or "No input". */
[[nodiscard]] juce::String describeInput(model::TrackInput input);

/** "MIDI: All", "MIDI: Keystation · Ch 2"... */
[[nodiscard]] juce::String describeMidiInput(const model::MidiInput& input);

/** "Off", "Auto" or "On". */
[[nodiscard]] juce::String describeMonitoring(model::MonitoringMode mode);

/**
    A track's recording controls, shared by the track headers and the mixer strips:
    arm (R), input monitoring (cycles off, auto, on) and the input: for an audio track
    a menu of the device's input channels, mono and stereo pairs; for a MIDI track the
    MIDI devices (or all of them) and a channel (or all). Edits go through the Session.
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
    void showMidiInputMenu(const model::Track& track);

    controller::Session& session;
    const model::ObjectId trackId;
};

} // namespace daw::ui::desktop
