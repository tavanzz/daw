#include "daw/ui/desktop/ArrangeView.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>

namespace daw::ui::desktop
{

// Headers and lanes side by side, so they scroll up and down together.
class ArrangeView::TrackArea final : public juce::Component
{
public:
    explicit TrackArea(ArrangeState& state)
        : headers(state)
        , lanes(state)
    {
        addAndMakeVisible(headers);
        addAndMakeVisible(lanes);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        headers.setBounds(area.removeFromLeft(ArrangeState::headerWidth));
        lanes.setBounds(area);
    }

    TrackHeaderList headers;
    TrackLanes lanes;
};

ArrangeView::ArrangeView(controller::Session& session)
    : state(session)
    , trackArea(std::make_unique<TrackArea>(state))
{
    state.onViewChanged = [this] { viewChanged(); };
    state.onOpenClip = [this](model::ObjectId clipId) {
        if (onOpenClip)
            onOpenClip(clipId);
    };
    trackArea->lanes.onProblem = [this](const juce::String& title, const juce::String& message) {
        if (onProblem)
            onProblem(title, message);
    };

    addAndMakeVisible(ruler);

    trackViewport.setViewedComponent(trackArea.get(), false);
    trackViewport.setScrollBarsShown(true, false);
    trackViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    addAndMakeVisible(trackViewport);

    timelineScrollBar.addListener(this);
    timelineScrollBar.setAutoHide(false);
    addAndMakeVisible(timelineScrollBar);
}

ArrangeView::~ArrangeView()
{
    timelineScrollBar.removeListener(this);
}

void ArrangeView::duplicateSelection()
{
    const auto copies =
        state.session.duplicateClips({state.selection.begin(), state.selection.end()});
    if (!copies.empty())
        state.selection = {copies.begin(), copies.end()};
    trackArea->lanes.contentChanged();
}

void ArrangeView::refresh()
{
    auto& session = state.session;

    if (shownRevision != session.getRevision())
    {
        shownRevision = session.getRevision();

        // Forget selected clips that no longer exist.
        std::erase_if(state.selection, [&session](model::ObjectId id) {
            return !session.getProject().findClip(id).has_value();
        });

        trackArea->headers.refresh();
        resized(); // the number of tracks may have changed
        trackArea->lanes.contentChanged();
        ruler.repaint();
        updateScrollBar();
    }

    const auto playhead = session.getPlayhead();
    if (session.getTransportState() == engine::TransportState::playing &&
        !juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() &&
        state.viewport.keepInView(playhead))
        viewChanged();

    trackArea->lanes.setPlayhead(playhead,
                                 session.getTransportState() == engine::TransportState::playing);
    ruler.setPlayhead(playhead);

    // Recordings grow on screen.
    if (session.isRecording())
        trackArea->lanes.contentChanged();
}

void ArrangeView::updateMeters(double elapsedSeconds)
{
    trackArea->headers.updateMeters(elapsedSeconds);
}

void ArrangeView::zoomIn()
{
    state.viewport.zoomAround(2.0, state.viewport.ticksToX(state.session.getPlayhead()));
    viewChanged();
}

void ArrangeView::zoomOut()
{
    state.viewport.zoomAround(0.5, state.viewport.ticksToX(state.session.getPlayhead()));
    viewChanged();
}

void ArrangeView::zoomToFit()
{
    const auto end = std::max(state.session.getProject().getEnd(),
                              state.session.getProject().getTempoMap().barStart(4));
    state.viewport.showRange(0, end + end / 20);
    viewChanged();
}

void ArrangeView::deleteSelection()
{
    state.session.deleteClips({state.selection.begin(), state.selection.end()});
    state.selection.clear();
}

void ArrangeView::setShowingAutomation(bool show)
{
    state.showAutomation = show;
    trackArea->headers.refresh();
    resized();
    trackArea->lanes.contentChanged();
}

model::Ticks ArrangeView::getInsertPosition() const
{
    return state.session.getPlayhead();
}

void ArrangeView::resized()
{
    auto area = getLocalBounds();
    const int scrollBarHeight = 12;

    auto rulerArea = area.removeFromTop(TimelineRuler::height);
    rulerArea.removeFromLeft(ArrangeState::headerWidth);
    ruler.setBounds(rulerArea);

    auto barArea = area.removeFromBottom(scrollBarHeight);
    barArea.removeFromLeft(ArrangeState::headerWidth);
    timelineScrollBar.setBounds(barArea);

    trackViewport.setBounds(area);

    // One empty lane below the last row, to drop files onto.
    const int numRows = state.numRows();
    const int contentWidth = area.getWidth() - trackViewport.getScrollBarThickness();
    const int contentHeight =
        std::max(area.getHeight(), (numRows + 1) * ArrangeState::laneHeight);
    trackArea->setSize(contentWidth, contentHeight);

    state.viewport.setWidth(std::max(1, contentWidth - ArrangeState::headerWidth));
    updateScrollBar();
}

void ArrangeView::paint(juce::Graphics& g)
{
    // The corner left of the ruler, above the track headers.
    ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(),
                                            appearance::ColourRole::panelAlt, false);
}

void ArrangeView::scrollBarMoved(juce::ScrollBar*, double newRangeStart)
{
    state.viewport.setStart(static_cast<model::Ticks>(newRangeStart));
    trackArea->lanes.contentChanged();
    ruler.repaint();
}

void ArrangeView::viewChanged()
{
    trackArea->lanes.contentChanged();
    ruler.repaint();
    updateScrollBar();
}

void ArrangeView::updateScrollBar()
{
    const auto& viewport = state.viewport;
    const auto visible = static_cast<double>(viewport.getEnd() - viewport.getStart());
    const auto end = std::max(static_cast<double>(state.session.getProject().getEnd()) * 1.1,
                              static_cast<double>(viewport.getEnd()));

    timelineScrollBar.setRangeLimits(0.0, end, juce::dontSendNotification);
    timelineScrollBar.setCurrentRange(static_cast<double>(viewport.getStart()), visible,
                                      juce::dontSendNotification);
}

} // namespace daw::ui::desktop
