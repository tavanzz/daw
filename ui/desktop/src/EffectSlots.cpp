#include "daw/ui/desktop/EffectSlots.h"

#include "daw/engine/fx/BuiltInEffects.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>

namespace daw::ui::desktop
{

juce::String effectTypeName(const juce::String& type)
{
    for (const auto& effect : engine::fx::builtInEffects())
        if (type == effect.type)
            return effect.name;
    return type;
}

EffectSlots::EffectSlots(controller::Session& sessionToEdit, controller::ChainOwner chainOwner)
    : session(sessionToEdit)
    , owner(chainOwner)
{
    refresh(true);
}

void EffectSlots::refresh(bool projectChanged)
{
    std::vector<Row> now;
    if (projectChanged)
    {
        const auto effects = session.getEffects(owner);
        for (int i = 0; i < effects.getNumEffects(); ++i)
        {
            const auto effect = effects.getEffect(i);
            now.push_back({effect.getId(), effectTypeName(effect.getType()), effect.isBypassed(), false});
        }
    }
    else
    {
        now = rows;
    }

    for (auto& row : now)
    {
        const auto instance = session.getEffectInstance(row.id);
        row.failed = instance != nullptr && instance->hasFailed();
    }

    if (now != rows)
    {
        rows = std::move(now);
        scroll = std::clamp(scroll, 0, std::max(0, static_cast<int>(rows.size()) + 1 - visibleRows()));
        repaint();
    }
}

int EffectSlots::visibleRows() const noexcept
{
    return std::max(1, getHeight() / rowHeight);
}

int EffectSlots::rowAt(float y) const noexcept
{
    return static_cast<int>(y) / rowHeight + scroll;
}

void EffectSlots::paint(juce::Graphics& g)
{
    const auto& look = ThemedLookAndFeel::get(*this);
    look.drawSlotList(g, getLocalBounds().toFloat());

    const int count = static_cast<int>(rows.size());
    for (int visible = 0; visible < visibleRows(); ++visible)
    {
        const int index = visible + scroll;
        const auto area = juce::Rectangle<int>(0, visible * rowHeight, getWidth(), rowHeight).reduced(1);

        if (index < count)
        {
            const auto& row = rows[static_cast<std::size_t>(index)];
            look.drawEffectSlot(g, area.toFloat(), row.name, row.bypassed, row.failed);
        }
        else if (index == count)
        {
            look.drawEmptySlot(g, area.toFloat(), "+ Effect");
        }
    }
}

void EffectSlots::mouseDown(const juce::MouseEvent& event)
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

    const auto row = rows[static_cast<std::size_t>(index)];
    if (event.mods.isPopupMenu())
        showEffectMenu(row, index);
    else if (onOpen)
        onOpen(row.id);
}

void EffectSlots::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int most = std::max(0, static_cast<int>(rows.size()) + 1 - visibleRows());
    scroll = std::clamp(scroll + (wheel.deltaY < 0.0f ? 1 : -1), 0, most);
    repaint();
}

void EffectSlots::showEffectMenu(const Row& row, int index)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Open Editor");
    menu.addItem(2, row.bypassed ? "Switch On" : "Bypass");
    if (row.failed)
        menu.addItem(3, "Reactivate");
    menu.addSeparator();
    menu.addItem(4, "Move Up", index > 0);
    menu.addItem(5, "Move Down", index + 1 < static_cast<int>(rows.size()));
    menu.addSeparator();
    menu.addItem(6, "Remove");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [this, row, index](int choice) {
                           switch (choice)
                           {
                           case 1:
                               if (onOpen)
                                   onOpen(row.id);
                               break;
                           case 2:
                               session.setEffectBypassed(row.id, !row.bypassed);
                               break;
                           case 3:
                               session.reviveEffect(row.id);
                               break;
                           case 4:
                               session.moveEffect(row.id, index - 1);
                               break;
                           case 5:
                               session.moveEffect(row.id, index + 1);
                               break;
                           case 6:
                               session.removeEffect(row.id);
                               break;
                           default:
                               break;
                           }

                           if (const auto problem = session.takeEditProblem(); problem.isNotEmpty())
                               juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                      "Effects", problem);
                       });
}

void EffectSlots::showAddMenu()
{
    juce::PopupMenu menu;
    const auto& effects = engine::fx::builtInEffects();
    for (std::size_t i = 0; i < effects.size(); ++i)
        menu.addItem(static_cast<int>(i) + 1, effects[i].name);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int choice) {
        const auto& list = engine::fx::builtInEffects();
        if (choice <= 0 || static_cast<std::size_t>(choice) > list.size())
            return;

        const auto id = session.addEffect(owner, list[static_cast<std::size_t>(choice) - 1].type);
        if (id == 0)
        {
            if (const auto problem = session.takeEditProblem(); problem.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Effects", problem);
        }
        else if (onOpen)
        {
            onOpen(id);
        }
    });
}

} // namespace daw::ui::desktop
