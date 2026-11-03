#pragma once

#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/** The column left of the track lanes: one header per track with its name
    (double-click to rename), mute, solo, arm, input monitoring, its input and an
    input meter. Right-click for more. */
class TrackHeaderList final : public juce::Component
{
public:
    explicit TrackHeaderList(controller::Session& sessionToEdit);
    ~TrackHeaderList() override;

    /** Brings the headers in line with the project; call it when it changed. */
    void refresh();

    /** Input meters, from the session's input peaks of this refresh. */
    void updateMeters(double elapsedSeconds);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Header;

    controller::Session& session;
    std::vector<std::unique_ptr<Header>> headers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackHeaderList)
};

} // namespace daw::ui::desktop
