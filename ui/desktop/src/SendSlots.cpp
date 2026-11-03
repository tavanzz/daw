#include "daw/ui/desktop/SendSlots.h"

#include "daw/engine/dsp/FaderLaw.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace daw::ui::desktop
{

SendSlots::SendSlots(controller::Session& sessionToEdit, model::ObjectId track)
    : session(sessionToEdit)
    , trackId(track)
{
    refresh(true);
}

void SendSlots::refresh(bool projectChanged)
{
    std::vector<Row> now;
    const auto& project = session.getProject();
    const int trackIndex = project.indexOfTrack(trackId);
    if (trackIndex < 0)
        return;

    if (projectChanged || rows.empty())
    {
        const auto track = project.getTrack(trackIndex);
        for (int i = 0; i < track.getNumSends(); ++i)
        {
            const auto send = track.getSend(i);
            const int bus = project.indexOfBus(send.getBus());
            now.push_back({send.getId(), bus >= 0 ? project.getBus(bus).getName() : "?",
                           send.getLevel(), send.isPreFader(), false});
        }
    }
    else
    {
        now = rows;
    }

    for (auto& row : now)
    {
        const auto automated = session.getAutomatedValue(
            controller::ChainOwner::track(trackId), model::AutomationTarget::sendLevel(row.id));
        row.automated = automated.has_value();
        if (automated)
            row.level = engine::dsp::faderGain(static_cast<float>(*automated));
    }

    if (now != rows)
    {
        rows = std::move(now);
        repaint();
    }
}

int SendSlots::rowAt(float y) const noexcept
{
    return static_cast<int>(y) / rowHeight;
}

void SendSlots::paint(juce::Graphics& g)
{
    const auto& lookAndFeel = ThemedLookAndFeel::get(*this);
    lookAndFeel.drawSlotList(g, getLocalBounds().toFloat());

    const int count = static_cast<int>(rows.size());
    for (int index = 0; index <= count && (index + 1) * rowHeight <= getHeight(); ++index)
    {
        const auto area = juce::Rectangle<int>(0, index * rowHeight, getWidth(), rowHeight).reduced(1);
        if (index == count)
        {
            lookAndFeel.drawEmptySlot(g, area.toFloat(), "+ Send");
            break;
        }

        const auto& row = rows[static_cast<std::size_t>(index)];
        lookAndFeel.drawSendSlot(g, area.toFloat(), row.bus + (row.preFader ? " (pre)" : ""),
                                 static_cast<float>(engine::dsp::faderPosition(row.level)),
                                 row.automated);
    }
}

void SendSlots::setLevelFrom(const Row& row, float x)
{
    const float position = std::clamp(x / static_cast<float>(std::max(1, getWidth() - 2)), 0.0f, 1.0f);
    session.setSendLevel(trackId, row.id, engine::dsp::faderGain(position));
}

void SendSlots::mouseDown(const juce::MouseEvent& event)
{
    const int index = rowAt(event.position.y);
    const int count = static_cast<int>(rows.size());

    if (index == count)
    {
        showAddMenu();
        return;
    }
    if (index < 0 || index > count)
        return;

    const auto& row = rows[static_cast<std::size_t>(index)];
    if (event.mods.isPopupMenu())
    {
        showSendMenu(row);
        return;
    }

    if (row.automated)
        return;
    dragging = index;
    session.beginGesture("Send Level");
    setLevelFrom(row, event.position.x);
}

void SendSlots::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging >= 0 && dragging < static_cast<int>(rows.size()))
        setLevelFrom(rows[static_cast<std::size_t>(dragging)], event.position.x);
}

void SendSlots::mouseUp(const juce::MouseEvent&)
{
    if (dragging >= 0)
        session.endGesture();
    dragging = -1;
}

void SendSlots::mouseDoubleClick(const juce::MouseEvent& event)
{
    const int index = rowAt(event.position.y);
    if (index >= 0 && index < static_cast<int>(rows.size()) &&
        !rows[static_cast<std::size_t>(index)].automated)
        session.setSendLevel(trackId, rows[static_cast<std::size_t>(index)].id, 1.0);
}

void SendSlots::showSendMenu(const Row& row)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Pre-fader", true, row.preFader);
    menu.addSeparator();
    menu.addItem(2, "Remove Send");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, row](int choice) {
        if (choice == 1)
            session.setSendPreFader(trackId, row.id, !row.preFader);
        else if (choice == 2)
            session.removeSend(trackId, row.id);
    });
}

void SendSlots::showAddMenu()
{
    const auto& project = session.getProject();
    const int trackIndex = project.indexOfTrack(trackId);
    if (trackIndex < 0)
        return;

    juce::PopupMenu menu;
    const auto track = project.getTrack(trackIndex);
    std::vector<model::ObjectId> buses;
    for (int b = 0; b < project.getNumBuses(); ++b)
    {
        const auto bus = project.getBus(b);
        buses.push_back(bus.getId());
        menu.addItem(static_cast<int>(buses.size()), bus.getName(), track.indexOfSendTo(bus.getId()) < 0);
    }
    if (!buses.empty())
        menu.addSeparator();
    menu.addItem(10000, "New Bus");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [this, buses](int choice) {
                           if (choice == 10000)
                           {
                               // One undo step: the bus and the send to it.
                               session.beginGesture("New Bus");
                               session.addSend(trackId, session.addBus());
                               session.endGesture();
                           }
                           else if (choice > 0 && static_cast<std::size_t>(choice) <= buses.size())
                           {
                               session.addSend(trackId, buses[static_cast<std::size_t>(choice) - 1]);
                           }
                       });
}

} // namespace daw::ui::desktop
