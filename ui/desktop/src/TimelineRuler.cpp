#include "daw/ui/desktop/TimelineRuler.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <cmath>

namespace daw::ui::desktop
{

TimelineRuler::TimelineRuler(ArrangeState& sharedState)
    : state(sharedState)
{
    setTooltip("Click to move the playhead, drag to set the loop region (Cmd: no snapping)");
}

void TimelineRuler::setPlayhead(model::Ticks position)
{
    if (position == playhead)
        return;

    const auto oldX = static_cast<int>(state.viewport.ticksToX(playhead));
    const auto newX = static_cast<int>(state.viewport.ticksToX(position));
    playhead = position;
    repaint(oldX - 6, 0, 13, getHeight());
    repaint(newX - 6, 0, 13, getHeight());
}

void TimelineRuler::paint(juce::Graphics& g)
{
    const auto& viewport = state.viewport;
    const auto& project = state.session.getProject();
    const auto tempoMap = project.getTempoMap();
    const auto& look = ThemedLookAndFeel::get(*this);
    const auto rulerHeight = static_cast<float>(getHeight());
    const float half = std::floor(rulerHeight / 2.0f);

    look.drawRulerBackground(g, getLocalBounds().toFloat());

    // Loop region, across both rows.
    if (project.getLoopEnd() > project.getLoopStart())
        look.drawRulerLoop(g, static_cast<float>(viewport.ticksToX(project.getLoopStart())),
                                  static_cast<float>(viewport.ticksToX(project.getLoopEnd())),
                                  rulerHeight, project.isLoopEnabled());

    // Bars and beats above, minutes and seconds below.
    for (const auto& mark : controller::barRulerMarks(viewport, tempoMap))
    {
        const auto x = static_cast<float>(viewport.ticksToX(mark.position));
        look.drawRulerMark(g, x, mark.major ? 2.0f : 12.0f, half, mark.major);
        if (mark.label.isNotEmpty())
            look.drawRulerLabel(g, mark.label, {x + 3.0f, 1.0f, 60.0f, half - 3.0f},
                                       mark.major);
    }

    g.setColour(look.colour(appearance::ColourRole::separator));
    g.fillRect(0.0f, half, static_cast<float>(getWidth()), 1.0f);

    for (const auto& mark : controller::timeRulerMarks(viewport))
    {
        const auto x = static_cast<float>(viewport.ticksToX(mark.position));
        look.drawRulerMark(g, x, mark.major ? half + 2.0f : rulerHeight - 6.0f, rulerHeight,
                                  mark.major);
        if (mark.label.isNotEmpty())
            look.drawRulerLabel(g, mark.label, {x + 3.0f, half + 1.0f, 80.0f, half - 3.0f},
                                       false);
    }

    // The playhead: a small triangle pointing down at the tracks.
    look.drawPlayheadMarker(g, static_cast<float>(viewport.ticksToX(playhead)), rulerHeight);
}

void TimelineRuler::mouseDown(const juce::MouseEvent& event)
{
    dragStart = state.snap(state.viewport.xToTicks(event.position.x), event.mods);
    settingLoop = false;
}

void TimelineRuler::mouseDrag(const juce::MouseEvent& event)
{
    if (!settingLoop && event.getDistanceFromDragStart() < 4)
        return;

    auto& session = state.session;
    if (!settingLoop)
    {
        settingLoop = true;
        session.beginGesture("Loop Region");
        session.setLoopEnabled(true);
    }

    const auto here = state.snap(state.viewport.xToTicks(event.position.x), event.mods);
    session.setLoopRange(std::min(dragStart, here), std::max(dragStart, here));
    repaint();
}

void TimelineRuler::mouseUp(const juce::MouseEvent&)
{
    if (settingLoop)
    {
        state.session.endGesture();
        settingLoop = false;
        return;
    }

    state.session.seek(dragStart);
}

void TimelineRuler::mouseWheelMove(const juce::MouseEvent& event,
                                   const juce::MouseWheelDetails& wheel)
{
    if (!state.handleWheel(event, wheel))
    {
        // Plain vertical movement over the ruler scrolls the timeline too.
        state.viewport.scrollByPixels(-wheel.deltaY * 400.0);
        state.viewChanged();
    }
}

void TimelineRuler::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    state.handleMagnify(event, scaleFactor);
}

} // namespace daw::ui::desktop
