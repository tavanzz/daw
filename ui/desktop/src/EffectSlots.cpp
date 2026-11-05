#include "daw/ui/desktop/EffectSlots.h"

#include "daw/controller/PluginBrowser.h"
#include "daw/engine/fx/BuiltInEffects.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>
#include <map>

namespace daw::ui::desktop
{

namespace
{

constexpr int firstPluginItem = 1000;

// The catalogue's effects or instruments: favourites first, then a submenu per
// manufacturer. Their descriptions go into plugins, item firstPluginItem + index.
void addPluginItems(juce::PopupMenu& menu, const controller::PluginCatalogue* catalogue, bool instruments,
                    std::vector<juce::PluginDescription>& plugins)
{
    plugins.clear();
    if (catalogue == nullptr)
        return;
    controller::PluginQuery query;
    query.kind = instruments ? controller::PluginQuery::Kind::instruments : controller::PluginQuery::Kind::effects;
    std::map<juce::String, juce::PopupMenu> byMaker;
    juce::PopupMenu favourites;
    for (const auto& item : controller::browserItems(*catalogue, query))
    {
        if (!item.plugin)
            continue;
        const int id = firstPluginItem + static_cast<int>(plugins.size());
        plugins.push_back(*item.plugin);
        const auto text = item.name + " (" + item.format + ")";
        if (item.favourite)
            favourites.addItem(id, text);
        const auto maker = item.manufacturer.isNotEmpty() ? item.manufacturer : juce::String("Other");
        byMaker[maker].addItem(id, text);
    }
    if (plugins.empty())
        return;
    menu.addSeparator();
    if (favourites.getNumItems() > 0)
        menu.addSubMenu("Favourites", favourites);
    juce::PopupMenu all;
    for (auto& [maker, items] : byMaker)
        all.addSubMenu(maker, items);
    menu.addSubMenu("Plugins", all);
}

const controller::PluginCatalogue& catalogueOrNone(const controller::Session& session)
{
    static const controller::PluginCatalogue none;
    const auto* catalogue = session.getPluginCatalogue();
    return catalogue != nullptr ? *catalogue : none;
}

juce::String nameOf(const model::Effect& effect)
{
    if (effect.getType() == model::pluginEffectType)
        if (const auto plugin = effect.getPlugin())
            return plugin->name;
    return effectTypeName(effect.getType());
}

} // namespace

juce::String effectTypeName(const juce::String& type)
{
    for (const auto& effect : engine::fx::builtInEffects())
        if (type == effect.type)
            return effect.name;
    for (const auto& instrument : engine::fx::builtInInstruments())
        if (type == instrument.type)
            return instrument.name;
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
        if (owner.kind == controller::ChainOwner::Kind::track)
        {
            const auto& project = session.getProject();
            const int index = project.indexOfTrack(owner.id);
            if (index >= 0 && project.getTrack(index).isMidi())
            {
                if (const auto instrument = project.getTrack(index).getInstrument())
                    now.push_back({instrument->getId(), nameOf(*instrument), instrument->isBypassed(), false, true,
                                   instrument->getType() == model::pluginEffectType});
                else
                    now.push_back({0, {}, false, false, true});
            }
        }

        const auto effects = session.getEffects(owner);
        for (int i = 0; i < effects.getNumEffects(); ++i)
        {
            const auto effect = effects.getEffect(i);
            now.push_back({effect.getId(), nameOf(effect), effect.isBypassed(), false, false,
                           effect.getType() == model::pluginEffectType});
        }
    }
    else
    {
        now = rows;
    }

    const auto& loads = session.getPluginLoads();
    for (auto& row : now)
    {
        const auto instance = session.getEffectInstance(row.id);
        row.failed = instance != nullptr && instance->hasFailed();
        if (row.isPlugin)
        {
            row.status = session.getPluginStatus(row.id);
            const auto* load = loads.find(row.id);
            row.slow = load != nullptr && load->slow;
        }
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
            using Mark = ThemedLookAndFeel::SlotMark;
            const auto mark = row.failed                                                  ? Mark::failed
                              : row.isPlugin && row.status != controller::PluginStatus::ready ? Mark::absent
                              : row.slow                                                  ? Mark::slow
                                                                                          : Mark::none;
            if (row.instrument && row.id == 0)
                look.drawEmptySlot(g, area.toFloat(), "+ Instrument");
            else
                look.drawEffectSlot(g, area.toFloat(), textOf(row), row.bypassed, mark);
        }
        else if (index == count)
        {
            look.drawEmptySlot(g, area.toFloat(), "+ Effect");
        }
    }

    if (dropRow >= 0)
    {
        g.setColour(look.colour(appearance::ColourRole::focus));
        g.fillRect(0, (dropRow - scroll) * rowHeight - 1, getWidth(), 2);
    }
}

juce::String EffectSlots::textOf(const Row& row)
{
    if (!row.isPlugin)
        return row.name;
    switch (row.status)
    {
    case controller::PluginStatus::loading:
        return row.name + " (loading)";
    case controller::PluginStatus::missing:
        return row.name + " (missing)";
    case controller::PluginStatus::failed:
        return row.name + " (failed)";
    case controller::PluginStatus::off:
        return row.name + " (off)";
    case controller::PluginStatus::ready:
        break;
    }
    return row.name;
}

juce::StringArray EffectSlots::getRowTexts() const
{
    juce::StringArray texts;
    for (const auto& row : rows)
        texts.add(row.instrument && row.id == 0 ? juce::String("+ Instrument") : textOf(row));
    return texts;
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
    if (row.instrument)
    {
        if (row.id == 0 || event.mods.isPopupMenu())
            showInstrumentMenu(row);
        else if (onOpen)
            onOpen(row.id);
        return;
    }
    if (event.mods.isPopupMenu())
        showEffectMenu(row, index - firstEffectRow());
    else if (onOpen)
        onOpen(row.id);
}

int EffectSlots::firstEffectRow() const noexcept
{
    return !rows.empty() && rows.front().instrument ? 1 : 0;
}

void EffectSlots::reportProblem()
{
    if (const auto problem = session.takeEditProblem(); problem.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Effects",
                                               problem);
}

void EffectSlots::showInstrumentMenu(const Row& row)
{
    constexpr int openItem = 1;
    constexpr int bypassItem = 2;
    constexpr int reactivateItem = 3;
    constexpr int removeItem = 4;
    constexpr int loadItem = 5;
    constexpr int firstType = 100;

    juce::PopupMenu menu;
    const auto& instruments = engine::fx::builtInInstruments();
    if (row.id != 0)
    {
        menu.addItem(openItem, "Open Editor");
        menu.addItem(bypassItem, row.bypassed ? "Switch On" : "Bypass");
        if (row.failed)
            menu.addItem(reactivateItem, "Reactivate");
        menu.addSeparator();
        if (row.isPlugin && row.status == controller::PluginStatus::off)
            menu.addItem(loadItem, "Load Plugin");
        menu.addSeparator();
        juce::PopupMenu change;
        for (std::size_t i = 0; i < instruments.size(); ++i)
            change.addItem(firstType + static_cast<int>(i), instruments[i].name);
        addPluginItems(change, session.getPluginCatalogue(), true, menuPlugins);
        menu.addSubMenu("Change Instrument", change);
        menu.addItem(removeItem, "Remove Instrument");
    }
    else
    {
        for (std::size_t i = 0; i < instruments.size(); ++i)
            menu.addItem(firstType + static_cast<int>(i), instruments[i].name);
        addPluginItems(menu, session.getPluginCatalogue(), true, menuPlugins);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, row](int choice) {
        const auto& list = engine::fx::builtInInstruments();
        if (choice == openItem && onOpen)
            onOpen(row.id);
        else if (choice == bypassItem)
            session.setEffectBypassed(row.id, !row.bypassed);
        else if (choice == reactivateItem)
            session.reviveEffect(row.id);
        else if (choice == removeItem)
            session.removeInstrument(owner.id);
        else if (choice == loadItem)
            session.allowPlugin(row.id);
        else if (choice >= firstPluginItem &&
                 static_cast<std::size_t>(choice - firstPluginItem) < menuPlugins.size())
        {
            const auto id = session.setInstrumentPlugin(
                owner.id, menuPlugins[static_cast<std::size_t>(choice - firstPluginItem)]);
            if (id != 0 && onOpen)
                onOpen(id);
        }
        else if (choice >= firstType && static_cast<std::size_t>(choice - firstType) < list.size())
        {
            const auto id = session.setInstrument(owner.id, list[static_cast<std::size_t>(choice - firstType)].type);
            if (id != 0 && onOpen)
                onOpen(id);
        }
        reportProblem();
    });
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
    if (row.isPlugin && row.status == controller::PluginStatus::off)
        menu.addItem(7, "Load Plugin");
    menu.addSeparator();
    menu.addItem(4, "Move Up", index > 0);
    menu.addItem(5, "Move Down", index + 1 + firstEffectRow() < static_cast<int>(rows.size()));
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
                           case 7:
                               session.allowPlugin(row.id);
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
    addPluginItems(menu, session.getPluginCatalogue(), false, menuPlugins);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int choice) {
        const auto& list = engine::fx::builtInEffects();
        model::ObjectId id = 0;
        if (choice >= firstPluginItem && static_cast<std::size_t>(choice - firstPluginItem) < menuPlugins.size())
            id = session.addPlugin(owner, menuPlugins[static_cast<std::size_t>(choice - firstPluginItem)]);
        else if (choice > 0 && static_cast<std::size_t>(choice) <= list.size())
            id = session.addEffect(owner, list[static_cast<std::size_t>(choice) - 1].type);
        else
            return;

        if (id == 0)
            reportProblem();
        else if (onOpen)
            onOpen(id);
    });
}

int EffectSlots::chainIndexAt(int y) const noexcept
{
    const int effects = static_cast<int>(rows.size()) - firstEffectRow();
    const int row = (y + rowHeight / 2) / rowHeight + scroll;
    return std::clamp(row - firstEffectRow(), 0, effects);
}

bool EffectSlots::isInterestedInDragSource(const SourceDetails& details)
{
    return controller::browserItemFor(catalogueOrNone(session), details.description.toString()).has_value();
}

void EffectSlots::itemDragMove(const SourceDetails& details)
{
    const auto item = controller::browserItemFor(catalogueOrNone(session), details.description.toString());
    // An instrument takes the instrument's row; an effect goes between effects.
    const int row = item && item->isInstrument ? 0 : chainIndexAt(details.localPosition.y) + firstEffectRow();
    if (row != dropRow)
    {
        dropRow = row;
        repaint();
    }
}

void EffectSlots::itemDragExit(const SourceDetails&)
{
    dropRow = -1;
    repaint();
}

void EffectSlots::itemDropped(const SourceDetails& details)
{
    dropRow = -1;
    repaint();
    const auto item = controller::browserItemFor(catalogueOrNone(session), details.description.toString());
    if (!item)
        return;
    const auto id = controller::addFromBrowser(
        session, *item, {controller::BrowserTarget::Kind::chain, owner, chainIndexAt(details.localPosition.y)});
    if (id == 0)
        reportProblem();
    else if (onOpen)
        onOpen(id);
}

} // namespace daw::ui::desktop
