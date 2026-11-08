#include "daw/ui/desktop/PluginBrowserPanel.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

namespace
{

constexpr int rowHeight = 34;
constexpr int starWidth = 26;
constexpr int allItems = 1; // the first entry of every filter menu

} // namespace

PluginBrowserPanel::PluginBrowserPanel(controller::Session& sessionToEdit, controller::PluginLibrary* pluginLibrary)
    : session(sessionToEdit)
    , library(pluginLibrary)
{
    title.setText("Plugins", juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(title, appearance::FontRole::heading);
    addAndMakeVisible(title);

    searchField.onTextChange = [this] { search(); };
    addAndMakeVisible(searchField);

    formatMenu.addItem("All Formats", allItems);
    formatMenu.addItem(controller::builtInFormat, allItems + 1);
    const auto formats = controller::PluginCatalogue::hostedFormats();
    for (int i = 0; i < formats.size(); ++i)
        formatMenu.addItem(formats[i] == "AudioUnit" ? juce::String("AU") : formats[i], allItems + 2 + i);
    formatMenu.setSelectedId(allItems, juce::dontSendNotification);
    formatMenu.onChange = [this] { search(); };
    addAndMakeVisible(formatMenu);

    kindMenu.addItem("Everything", allItems);
    kindMenu.addItem("Effects", allItems + 1);
    kindMenu.addItem("Instruments", allItems + 2);
    kindMenu.setSelectedId(allItems, juce::dontSendNotification);
    kindMenu.onChange = [this] { search(); };
    addAndMakeVisible(kindMenu);

    makerMenu.onChange = [this] { search(); };
    addAndMakeVisible(makerMenu);

    favouritesButton.onClick = [this] { search(); };
    addAndMakeVisible(favouritesButton);

    list.setModel(this);
    list.setRowHeight(rowHeight);
    addAndMakeVisible(list);

    ThemedLookAndFeel::setFontRole(footer, appearance::FontRole::caption);
    ThemedLookAndFeel::setColourRole(footer, appearance::ColourRole::textSecondary);
    addAndMakeVisible(footer);

    fillMakers();
    search();
    if (library != nullptr)
        shownRevision = library->getRevision();
    lookAndFeelChanged();
}

void PluginBrowserPanel::lookAndFeelChanged()
{
    searchField.setTextToShowWhenEmpty("Search",
                                       ThemedLookAndFeel::get(*this).colour(appearance::ColourRole::textSecondary));
}

PluginBrowserPanel::~PluginBrowserPanel()
{
    list.setModel(nullptr);
}

controller::PluginCatalogue& PluginBrowserPanel::catalogue() noexcept
{
    return library != nullptr ? library->getCatalogue() : ownCatalogue;
}

void PluginBrowserPanel::refresh()
{
    if (library == nullptr)
    {
        footer.setText({}, juce::dontSendNotification);
        return;
    }

    if (library->getRevision() != shownRevision)
    {
        shownRevision = library->getRevision();
        fillMakers();
        search();
    }

    juce::String line;
    if (library->isScanning())
    {
        const auto progress = library->getProgress();
        line = "Scanning " + juce::String(progress.done) + " of " + juce::String(progress.total) + "...";
    }
    else
    {
        line = juce::String(static_cast<int>(catalogue().getPlugins().size())) + " plugins";
    }
    if (footer.getText() != line)
        footer.setText(line, juce::dontSendNotification);
}

void PluginBrowserPanel::fillMakers()
{
    const auto chosen = makerMenu.getText();
    makerMenu.clear(juce::dontSendNotification);
    makerMenu.addItem("All Makers", allItems);
    const auto makers = catalogue().getManufacturers();
    for (int i = 0; i < makers.size(); ++i)
        makerMenu.addItem(makers[i], allItems + 1 + i);
    const int index = makers.indexOf(chosen);
    makerMenu.setSelectedId(index >= 0 ? allItems + 1 + index : allItems, juce::dontSendNotification);
}

void PluginBrowserPanel::search()
{
    controller::PluginQuery query;
    query.text = searchField.getText();
    if (formatMenu.getSelectedId() > allItems)
    {
        const int index = formatMenu.getSelectedId() - allItems - 2;
        query.formats.insert(index < 0 ? controller::builtInFormat
                                       : controller::PluginCatalogue::hostedFormats()[index]);
    }
    query.kind = kindMenu.getSelectedId() == allItems + 1   ? controller::PluginQuery::Kind::effects
                 : kindMenu.getSelectedId() == allItems + 2 ? controller::PluginQuery::Kind::instruments
                                                            : controller::PluginQuery::Kind::any;
    if (makerMenu.getSelectedId() > allItems)
        query.manufacturer = makerMenu.getText();
    query.favouritesOnly = favouritesButton.getToggleState();

    items = controller::browserItems(catalogue(), query);
    list.updateContent();
    list.repaint();
}

void PluginBrowserPanel::setSearchText(const juce::String& text)
{
    searchField.setText(text, false); // the field tells of changes later: search now
    search();
}

void PluginBrowserPanel::setKind(controller::PluginQuery::Kind kind)
{
    kindMenu.setSelectedId(kind == controller::PluginQuery::Kind::effects       ? allItems + 1
                           : kind == controller::PluginQuery::Kind::instruments ? allItems + 2
                                                                                : allItems,
                           juce::sendNotificationSync);
}

void PluginBrowserPanel::setFavouritesOnly(bool only)
{
    favouritesButton.setToggleState(only, juce::sendNotificationSync);
}

void PluginBrowserPanel::toggleFavourite(int row)
{
    if (row < 0 || row >= static_cast<int>(items.size()))
        return;
    const auto& item = items[static_cast<std::size_t>(row)];
    catalogue().setFavourite(item.key(), !item.favourite);
    search();
}

model::ObjectId PluginBrowserPanel::addRow(int row)
{
    if (row < 0 || row >= static_cast<int>(items.size()))
        return 0;
    const auto target = targetForDoubleClick ? targetForDoubleClick() : controller::BrowserTarget{};
    const auto id = controller::addFromBrowser(session, items[static_cast<std::size_t>(row)], target);
    if (id != 0 && onAdded)
        onAdded(id);
    return id;
}

void PluginBrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    title.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    searchField.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    auto filters = area.removeFromTop(24);
    formatMenu.setBounds(filters.removeFromLeft((filters.getWidth() - 6) / 2));
    filters.removeFromLeft(6);
    kindMenu.setBounds(filters);
    area.removeFromTop(6);
    auto second = area.removeFromTop(24);
    favouritesButton.setBounds(second.removeFromRight(100));
    second.removeFromRight(6);
    makerMenu.setBounds(second);
    area.removeFromTop(8);
    footer.setBounds(area.removeFromBottom(20));
    area.removeFromBottom(4);
    list.setBounds(area);
}

void PluginBrowserPanel::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::panel, false);
}

int PluginBrowserPanel::getNumRows()
{
    return static_cast<int>(items.size());
}

void PluginBrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int rowWidth, int height, bool selected)
{
    if (row < 0 || row >= static_cast<int>(items.size()))
        return;
    const auto& item = items[static_cast<std::size_t>(row)];
    const auto& look = ThemedLookAndFeel::get(*this);
    auto area = juce::Rectangle<int>(0, 0, rowWidth, height).reduced(2, 1);
    if (selected)
    {
        g.setColour(look.colour(appearance::ColourRole::raised));
        g.fillPath(look.shapeOf(area.toFloat(), CornerSize::small));
    }

    // The favourite's star: filled when it is one.
    const auto star = area.removeFromRight(starWidth).toFloat();
    juce::Path shape;
    shape.addStar(star.getCentre(), 5, 3.2f, 7.5f, 0.0f);
    g.setColour(look.colour(item.favourite ? appearance::ColourRole::highlight : appearance::ColourRole::textSecondary));
    if (item.favourite)
        g.fillPath(shape);
    else
        g.strokePath(shape, juce::PathStrokeType(1.0f));

    g.setFont(look.font(appearance::FontRole::label));
    area.removeFromLeft(6);
    g.setColour(look.colour(appearance::ColourRole::text));
    g.drawText(item.name, area.removeFromTop(area.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
    g.setColour(look.colour(appearance::ColourRole::textSecondary));
    g.setFont(look.font(appearance::FontRole::caption));
    const auto format = item.format == "AudioUnit" ? juce::String("AU") : item.format;
    const auto kind = item.isInstrument ? juce::String("Instrument") : juce::String("Effect");
    const auto maker = item.manufacturer.isNotEmpty() ? item.manufacturer + "  |  " : juce::String();
    g.drawText(maker + format + "  |  " + kind, area, juce::Justification::topLeft, true);
}

void PluginBrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    if (event.x >= list.getVisibleRowWidth() - starWidth - 2)
        toggleFavourite(row);
}

void PluginBrowserPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent& event)
{
    if (event.x < list.getVisibleRowWidth() - starWidth - 2)
        addRow(row);
}

juce::var PluginBrowserPanel::getDragSourceDescription(const juce::SparseSet<int>& rows)
{
    if (rows.size() != 1)
        return {};
    const int row = rows[0];
    if (row < 0 || row >= static_cast<int>(items.size()))
        return {};
    return items[static_cast<std::size_t>(row)].dragDescription();
}

juce::String PluginBrowserPanel::getTooltipForRow(int row)
{
    if (row < 0 || row >= static_cast<int>(items.size()))
        return {};
    const auto& item = items[static_cast<std::size_t>(row)];
    return item.name + (item.category.isNotEmpty() ? " (" + item.category + ")" : juce::String()) +
           ". Drag it onto a track or a mixer slot, or double-click it.";
}

} // namespace daw::ui::desktop
