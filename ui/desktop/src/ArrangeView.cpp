#include "daw/ui/desktop/ArrangeView.h"

#include "daw/controller/PluginBrowser.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>

namespace daw::ui::desktop
{

// Headers and lanes side by side, so they scroll up and down together. Items from
// the plugin browser are dropped on a row (a track, or in the automation view a bus
// or the master) or below the last one (a new track).
class ArrangeView::TrackArea final : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit TrackArea(ArrangeState& stateToUse)
        : state(stateToUse)
        , headers(stateToUse)
        , lanes(stateToUse)
    {
        addAndMakeVisible(headers);
        addAndMakeVisible(lanes);
        addChildComponent(highlight);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        headers.setBounds(area.removeFromLeft(ArrangeState::headerWidth));
        lanes.setBounds(area);
    }

    [[nodiscard]] controller::BrowserTarget targetAt(int y) const
    {
        const int row = y / ArrangeState::laneHeight;
        if (y < 0 || row >= state.numRows())
            return {};
        const auto owner = state.ownerOfRow(row);
        if (owner.kind == controller::ChainOwner::Kind::track)
            return {controller::BrowserTarget::Kind::track, owner};
        return {controller::BrowserTarget::Kind::chain, owner, -1};
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return item(details).has_value();
    }

    void itemDragMove(const SourceDetails& details) override
    {
        const int rows = state.numRows();
        const int row = std::clamp(details.localPosition.y / ArrangeState::laneHeight, 0, rows);
        highlight.setBounds(0, row * ArrangeState::laneHeight, getWidth(), ArrangeState::laneHeight);
        highlight.setVisible(true);
        highlight.toFront(false);
    }

    void itemDragExit(const SourceDetails&) override { highlight.setVisible(false); }

    void itemDropped(const SourceDetails& details) override
    {
        highlight.setVisible(false);
        if (const auto dropped = item(details))
        {
            const auto id = controller::addFromBrowser(state.session, *dropped, targetAt(details.localPosition.y));
            if (id != 0 && onAdded)
                onAdded(id);
            else if (id == 0 && onRefused)
                onRefused(state.session.takeEditProblem());
        }
    }

    std::function<void(model::ObjectId)> onAdded;
    std::function<void(const juce::String& problem)> onRefused;

    ArrangeState& state;
    TrackHeaderList headers;
    TrackLanes lanes;

private:
    // Where a browser item would go: the row's outline.
    class Highlight final : public juce::Component
    {
    public:
        Highlight() { setInterceptsMouseClicks(false, false); }

        void paint(juce::Graphics& g) override
        {
            g.setColour(ThemedLookAndFeel::get(*this).colour(appearance::ColourRole::focus));
            g.drawRect(getLocalBounds(), 2);
        }
    };

    [[nodiscard]] std::optional<controller::BrowserItem> item(const SourceDetails& details) const
    {
        static const controller::PluginCatalogue none;
        const auto* catalogue = state.session.getPluginCatalogue();
        return controller::browserItemFor(catalogue != nullptr ? *catalogue : none, details.description.toString());
    }

    Highlight highlight;
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
    trackArea->onAdded = [this](model::ObjectId effectId) {
        if (onEffectAdded)
            onEffectAdded(effectId);
    };
    trackArea->onRefused = [this](const juce::String& problem) {
        if (onProblem && problem.isNotEmpty())
            onProblem("It cannot go there", problem);
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

juce::DragAndDropTarget& ArrangeView::getDropTargetForTesting() noexcept
{
    return *trackArea;
}

controller::BrowserTarget ArrangeView::getSelectedTrackTarget() const
{
    const auto& project = state.session.getProject();
    for (const auto clipId : state.selection)
        if (const auto location = project.findClip(clipId))
            return {controller::BrowserTarget::Kind::track,
                    controller::ChainOwner::track(project.getTrack(location->trackIndex).getId())};
    return {};
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
