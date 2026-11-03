#pragma once

#include "daw/controller/Session.h"
#include "daw/ui/desktop/ArrangeState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/** The column left of the track lanes: one header per track with its name
    (double-click to rename), mute, solo, arm, input monitoring, its input and an
    input meter. Right-click for more. In the automation view the recording
    controls give way to the lane's target (a menu) and the automation mode (Read or
    Off), and the buses and the master get headers too. */
class TrackHeaderList final : public juce::Component
{
public:
    explicit TrackHeaderList(ArrangeState& sharedState);
    ~TrackHeaderList() override;

    /** Brings the headers in line with the project; call it when it changed. */
    void refresh();

    /** Input meters, from the session's input peaks of this refresh. */
    void updateMeters(double elapsedSeconds);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Header;
    class AutomationControls;
    class OwnerHeader;

    ArrangeState& state;
    controller::Session& session;
    std::vector<std::unique_ptr<Header>> headers;
    std::vector<std::unique_ptr<OwnerHeader>> ownerHeaders; // buses and master, automation view
    bool showingAutomation = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackHeaderList)
};

} // namespace daw::ui::desktop
