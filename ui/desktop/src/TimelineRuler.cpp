#include "daw/ui/desktop/TimelineRuler.h"

namespace daw::ui::desktop
{

namespace
{
const juce::Colour playheadColour(0xffffa726);
const juce::Colour loopOnColour(0xff2e7d32);
const juce::Colour loopOffColour(0xff555555);
} // namespace

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
    const int half = getHeight() / 2;

    g.fillAll(juce::Colour(0xff262626));

    // Loop region, across both rows.
    if (project.getLoopEnd() > project.getLoopStart())
    {
        const auto x0 = static_cast<float>(viewport.ticksToX(project.getLoopStart()));
        const auto x1 = static_cast<float>(viewport.ticksToX(project.getLoopEnd()));
        const auto colour = project.isLoopEnabled() ? loopOnColour : loopOffColour;
        g.setColour(colour.withAlpha(0.45f));
        g.fillRect(juce::Rectangle<float>(x0, 0.0f, x1 - x0, static_cast<float>(getHeight())));
        g.setColour(colour);
        g.drawLine(x0, 0.0f, x0, static_cast<float>(getHeight()), 2.0f);
        g.drawLine(x1, 0.0f, x1, static_cast<float>(getHeight()), 2.0f);
    }

    g.setFont(juce::FontOptions(12.0f));

    for (const auto& mark : controller::barRulerMarks(viewport, tempoMap))
    {
        const auto x = static_cast<float>(viewport.ticksToX(mark.position));
        g.setColour(mark.major ? juce::Colours::lightgrey : juce::Colours::grey);
        g.drawLine(x, mark.major ? 2.0f : 12.0f, x, static_cast<float>(half), 1.0f);
        if (mark.label.isNotEmpty())
            g.drawText(mark.label, juce::Rectangle<float>(x + 3.0f, 1.0f, 60.0f, 14.0f),
                       juce::Justification::centredLeft, false);
    }

    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawHorizontalLine(half, 0.0f, static_cast<float>(getWidth()));

    for (const auto& mark : controller::timeRulerMarks(viewport))
    {
        const auto x = static_cast<float>(viewport.ticksToX(mark.position));
        g.setColour(mark.major ? juce::Colours::lightgrey.darker(0.2f)
                               : juce::Colours::grey.darker());
        g.drawLine(x, static_cast<float>(mark.major ? half + 2 : getHeight() - 6), x,
                   static_cast<float>(getHeight()), 1.0f);
        if (mark.label.isNotEmpty())
            g.drawText(mark.label,
                       juce::Rectangle<float>(x + 3.0f, static_cast<float>(half + 1), 80.0f, 14.0f),
                       juce::Justification::centredLeft, false);
    }

    // The playhead: a small triangle pointing down at the tracks.
    const auto x = static_cast<float>(viewport.ticksToX(playhead));
    juce::Path marker;
    marker.addTriangle(x - 6.0f, static_cast<float>(getHeight() - 10), x + 6.0f,
                       static_cast<float>(getHeight() - 10), x, static_cast<float>(getHeight()));
    g.setColour(playheadColour);
    g.fillPath(marker);
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
