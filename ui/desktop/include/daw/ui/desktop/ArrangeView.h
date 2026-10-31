#pragma once

#include "daw/ui/desktop/ArrangeState.h"
#include "daw/ui/desktop/TimelineRuler.h"
#include "daw/ui/desktop/TrackHeaderList.h"
#include "daw/ui/desktop/TrackLanes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/**
    The arrange window: ruler on top, track headers on the left, the track lanes,
    and a scroll bar for the timeline. Tracks scroll vertically; the timeline zooms
    and scrolls with Cmd + wheel / pinch and horizontal wheel movement, and follows
    the playhead while playing.
*/
class ArrangeView final : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit ArrangeView(controller::Session& session);
    ~ArrangeView() override;

    /** Call from the UI's refresh timer: redraws what changed, moves the playhead. */
    void refresh();

    /** The tracks' input meters (after Session::takeInputPeaks()). */
    void updateMeters(double elapsedSeconds);

    void zoomIn();
    void zoomOut();
    void zoomToFit();

    [[nodiscard]] bool hasSelection() const noexcept { return !state.selection.empty(); }
    void deleteSelection();

    /** Where new material goes: the playhead. */
    [[nodiscard]] model::Ticks getInsertPosition() const;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class TrackArea;

    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void viewChanged();
    void updateScrollBar();

    ArrangeState state;
    TimelineRuler ruler{state};
    std::unique_ptr<TrackArea> trackArea;
    juce::Viewport trackViewport;
    juce::ScrollBar timelineScrollBar{false};

    juce::uint64 shownRevision = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArrangeView)
};

} // namespace daw::ui::desktop
