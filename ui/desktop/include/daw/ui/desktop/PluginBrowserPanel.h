#pragma once

#include "daw/controller/PluginBrowser.h"
#include "daw/controller/PluginLibrary.h"
#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/**
    The plugin browser, left of the tracks (B): the built-in effects and instruments
    and the catalogue's plugins, with a search field, filters by format, type and
    manufacturer, and favourites (the star of each row). Rows are dragged onto a
    track, a mixer slot or the empty space below the tracks; a double-click puts one
    where targetForDoubleClick says. While plugins are scanned, the bottom line says
    how far it got.
*/
class PluginBrowserPanel final : public juce::Component, private juce::ListBoxModel
{
public:
    static constexpr int width = 260;

    /** library: null when the app hosts no plugins (only the built-in ones are listed). */
    PluginBrowserPanel(controller::Session& sessionToEdit, controller::PluginLibrary* pluginLibrary);
    ~PluginBrowserPanel() override;

    /** Where a double-clicked item goes (default: a new track). */
    std::function<controller::BrowserTarget()> targetForDoubleClick;

    /** An effect or instrument was added (to open its editor). */
    std::function<void(model::ObjectId effectId)> onAdded;

    /** Call from the UI's refresh timer: reads the catalogue again when it changed. */
    void refresh();

    // For tests.
    [[nodiscard]] const std::vector<controller::BrowserItem>& getItems() const noexcept { return items; }
    void setSearchText(const juce::String& text);
    void setKind(controller::PluginQuery::Kind kind);
    void setFavouritesOnly(bool only);
    void toggleFavourite(int row);
    model::ObjectId addRow(int row);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

private:
    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
    juce::String getTooltipForRow(int row) override;

    [[nodiscard]] controller::PluginCatalogue& catalogue() noexcept;
    void fillMakers();
    void search();

    controller::Session& session;
    controller::PluginLibrary* library;
    controller::PluginCatalogue ownCatalogue; // without a library: built-in favourites
    juce::uint64 shownRevision = 0;
    std::vector<controller::BrowserItem> items;

    juce::Label title;
    juce::TextEditor searchField;
    juce::ComboBox formatMenu;
    juce::ComboBox kindMenu;
    juce::ComboBox makerMenu;
    juce::ToggleButton favouritesButton{"Favourites"};
    juce::ListBox list;
    juce::Label footer;
};

} // namespace daw::ui::desktop
