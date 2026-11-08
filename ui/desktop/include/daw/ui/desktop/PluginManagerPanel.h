#pragma once

#include "daw/controller/PluginLibrary.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/**
    File > Plugin Manager: scanning (new or changed plugins, everything again, cancel,
    how far it got), and three pages: the plugins found (with why one cannot be hosted),
    those not loaded (the blacklist: crashed, hung, no plugins, failed to load, with
    Retry) and the folders searched per format (the defaults, then the user's own, which
    can be added and removed; Audio Units come from the system). Changes go to the
    library, which saves the catalogue; a new folder is scanned at once.
*/
class PluginManagerPanel final : public juce::Component, private juce::Timer
{
public:
    enum class Page
    {
        plugins,
        notLoaded,
        folders
    };

    explicit PluginManagerPanel(controller::PluginLibrary& libraryToManage);
    ~PluginManagerPanel() override;

    void showPage(Page page);

    // For tests.
    [[nodiscard]] int getNumPluginRows() const noexcept { return static_cast<int>(plugins.size()); }
    [[nodiscard]] int getNumNotLoadedRows() const noexcept { return static_cast<int>(blocked.size()); }
    [[nodiscard]] juce::StringArray getFolderLines() const;
    void addFolder(const juce::String& format, const juce::File& folder);
    void removeFolder(const juce::String& format, int index);
    void retry(int row);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class PluginList;
    class NotLoadedList;
    class FolderList;

    void timerCallback() override;
    void readLibrary();
    void chooseFolder();

    controller::PluginLibrary& library;
    juce::uint64 shownRevision = 0;
    std::vector<controller::PluginInfo> plugins;
    std::vector<controller::PluginCatalogue::Blocked> blocked;
    juce::StringArray formats; // with folders of their own: VST3, LV2

    juce::Label heading;
    juce::TextButton scanNew{"Scan for New Plugins"};
    juce::TextButton scanAll{"Rescan All"};
    juce::TextButton cancel{"Cancel"};
    juce::Label progress;
    std::array<juce::TextButton, 3> pageButtons;
    Page page = Page::plugins;

    std::unique_ptr<PluginList> pluginModel;
    std::unique_ptr<NotLoadedList> notLoadedModel;
    std::unique_ptr<FolderList> folderModel;
    juce::ListBox pluginBox;
    juce::ListBox notLoadedBox;
    juce::TextButton retryButton{"Retry"};
    juce::Label notLoadedNote;

    juce::ComboBox folderFormat;
    juce::ListBox folderBox;
    juce::TextButton addFolderButton{"Add Folder..."};
    juce::TextButton removeFolderButton{"Remove"};
    juce::Label folderNote;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace daw::ui::desktop
