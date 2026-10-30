#pragma once

#include "daw/controller/Session.h"
#include "daw/controller/TimelineViewport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <set>

namespace daw::ui::desktop
{

/** What the parts of the arrange view (ruler, track lanes) share: the session, the
    visible part of the timeline, and the selected clips. */
struct ArrangeState
{
    static constexpr int laneHeight = 72;
    static constexpr int headerWidth = 200;

    explicit ArrangeState(controller::Session& sessionToEdit)
        : session(sessionToEdit)
    {
    }

    controller::Session& session;
    controller::TimelineViewport viewport;
    std::set<model::ObjectId> selection;

    /** Called after zooming or scrolling, so every part redraws. */
    std::function<void()> onViewChanged;

    /** Trackpad and wheel: Cmd + vertical zooms around the pointer, horizontal (or
        Shift + vertical) scrolls the timeline. Returns false for plain vertical
        movement, which scrolls the tracks instead. */
    bool handleWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        if (event.mods.isCommandDown())
        {
            viewport.zoomAround(std::pow(2.0, wheel.deltaY * 2.0), event.position.x);
        }
        else if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) || event.mods.isShiftDown())
        {
            const float delta = std::abs(wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY;
            viewport.scrollByPixels(-delta * 400.0);
        }
        else
        {
            return false;
        }

        viewChanged();
        return true;
    }

    void handleMagnify(const juce::MouseEvent& event, float scaleFactor)
    {
        viewport.zoomAround(scaleFactor, event.position.x);
        viewChanged();
    }

    void viewChanged()
    {
        if (onViewChanged)
            onViewChanged();
    }

    /** Where position snaps to, as set in the project, at this zoom. */
    [[nodiscard]] model::Ticks snap(model::Ticks position, const juce::ModifierKeys& mods) const
    {
        return session.makeSnapper(viewport.getTicksPerPixel(), mods.isCommandDown())(position);
    }
};

} // namespace daw::ui::desktop
