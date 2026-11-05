#include "daw/ui/desktop/PluginManagerPanel.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

namespace
{

constexpr int rowHeight = 22;

juce::String formatName(const juce::String& format)
{
    return format == "AudioUnit" ? juce::String("AU") : format;
}

// A row of text in columns: the first in the text colour, the others secondary.
void drawColumns(juce::Graphics& g, const juce::Component& owner, juce::Rectangle<int> area, bool selected,
                 const juce::StringArray& texts, const std::vector<float>& widths)
{
    const auto& look = ThemedLookAndFeel::get(owner);
    if (selected)
    {
        g.setColour(look.colour(appearance::ColourRole::raised));
        g.fillRect(area);
    }
    area.removeFromLeft(6);
    const auto total = static_cast<float>(area.getWidth());
    for (int i = 0; i < texts.size(); ++i)
    {
        const bool last = i + 1 == texts.size();
        const auto column = last ? area : area.removeFromLeft(juce::roundToInt(total * widths[static_cast<std::size_t>(i)]));
        g.setColour(look.colour(i == 0 ? appearance::ColourRole::text : appearance::ColourRole::textSecondary));
        g.setFont(look.font(i == 0 ? appearance::FontRole::label : appearance::FontRole::caption));
        g.drawText(texts[i], column.reduced(2, 0), juce::Justification::centredLeft, true);
    }
}

} // namespace

class PluginManagerPanel::PluginList final : public juce::ListBoxModel
{
public:
    explicit PluginList(PluginManagerPanel& panelToShow)
        : panel(panelToShow)
    {
    }

    int getNumRows() override { return static_cast<int>(panel.plugins.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows())
            return;
        const auto& info = panel.plugins[static_cast<std::size_t>(row)];
        const auto& description = info.description;
        drawColumns(g, panel.pluginBox, {0, 0, width, height}, selected,
                    {description.name, description.manufacturerName, formatName(description.pluginFormatName),
                     description.isInstrument ? juce::String("Instrument") : juce::String("Effect"),
                     info.isSupported() ? description.version : "Not hosted: " + info.unsupported},
                    {0.3f, 0.22f, 0.08f, 0.12f});
    }

private:
    PluginManagerPanel& panel;
};

class PluginManagerPanel::NotLoadedList final : public juce::ListBoxModel
{
public:
    explicit NotLoadedList(PluginManagerPanel& panelToShow)
        : panel(panelToShow)
    {
    }

    int getNumRows() override { return static_cast<int>(panel.blocked.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows())
            return;
        const auto& entry = panel.blocked[static_cast<std::size_t>(row)];
        const auto name = entry.item.format == "AudioUnit" ? entry.item.item
                                                           : juce::File(entry.item.item).getFileName();
        const auto why = controller::toString(entry.failure) +
                         (entry.detail.isNotEmpty() ? ": " + entry.detail : juce::String());
        drawColumns(g, panel.notLoadedBox, {0, 0, width, height}, selected,
                    {name, formatName(entry.item.format), why}, {0.4f, 0.08f});
    }

    void selectedRowsChanged(int) override { panel.retryButton.setEnabled(panel.notLoadedBox.getNumSelectedRows() > 0); }

private:
    PluginManagerPanel& panel;
};

class PluginManagerPanel::FolderList final : public juce::ListBoxModel
{
public:
    explicit FolderList(PluginManagerPanel& panelToShow)
        : panel(panelToShow)
    {
    }

    int getNumRows() override { return panel.getFolderLines().size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        const auto lines = panel.getFolderLines();
        if (row < 0 || row >= lines.size())
            return;
        const int defaults = lines.size() - customCount();
        drawColumns(g, panel.folderBox, {0, 0, width, height}, selected,
                    {lines[row], row < defaults ? juce::String("default") : juce::String("yours")}, {0.85f});
    }

    void selectedRowsChanged(int row) override
    {
        const int defaults = panel.getFolderLines().size() - customCount();
        panel.removeFolderButton.setEnabled(row >= defaults);
    }

    [[nodiscard]] int customCount() const
    {
        return panel.library.getCatalogue().getCustomFolders(panel.folderFormat.getText()).size();
    }

private:
    PluginManagerPanel& panel;
};

PluginManagerPanel::PluginManagerPanel(controller::PluginLibrary& libraryToManage)
    : library(libraryToManage)
{
    for (const auto& format : controller::PluginCatalogue::hostedFormats())
        if (format != "AudioUnit")
            formats.add(format);

    heading.setText("Plugin Manager", juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(heading, appearance::FontRole::heading);
    addAndMakeVisible(heading);

    scanNew.onClick = [this] { library.scan(false); };
    scanAll.onClick = [this] { library.scan(true); };
    cancel.onClick = [this] { library.cancelScan(); };
    addAndMakeVisible(scanNew);
    addAndMakeVisible(scanAll);
    addAndMakeVisible(cancel);
    ThemedLookAndFeel::setFontRole(progress, appearance::FontRole::caption);
    ThemedLookAndFeel::setColourRole(progress, appearance::ColourRole::textSecondary);
    addAndMakeVisible(progress);

    const juce::StringArray pageNames{"Plugins", "Not Loaded", "Folders"};
    for (std::size_t i = 0; i < pageButtons.size(); ++i)
    {
        auto& button = pageButtons[i];
        button.setButtonText(pageNames[static_cast<int>(i)]);
        button.setClickingTogglesState(true);
        button.setRadioGroupId(1);
        ThemedLookAndFeel::setColourRole(button, appearance::ColourRole::accent);
        button.onClick = [this, i] { showPage(static_cast<Page>(i)); };
        addAndMakeVisible(button);
    }

    pluginModel = std::make_unique<PluginList>(*this);
    notLoadedModel = std::make_unique<NotLoadedList>(*this);
    folderModel = std::make_unique<FolderList>(*this);
    pluginBox.setModel(pluginModel.get());
    notLoadedBox.setModel(notLoadedModel.get());
    notLoadedBox.setMultipleSelectionEnabled(true);
    folderBox.setModel(folderModel.get());
    for (auto* box : {&pluginBox, &notLoadedBox, &folderBox})
    {
        box->setRowHeight(rowHeight);
        addChildComponent(*box);
    }

    retryButton.setEnabled(false);
    retryButton.onClick = [this] {
        const auto rows = notLoadedBox.getSelectedRows();
        // Retrying takes an item off the list: the last rows first.
        for (int i = rows.size(); --i >= 0;)
            retry(rows[i]);
    };
    addChildComponent(retryButton);
    notLoadedNote.setText("These crashed, hung or failed when they were scanned, in a process of their own. "
                          "Retry scans one again; a file that changed is tried again by the next scan.",
                          juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(notLoadedNote, appearance::FontRole::caption);
    ThemedLookAndFeel::setColourRole(notLoadedNote, appearance::ColourRole::textSecondary);
    addChildComponent(notLoadedNote);

    for (int i = 0; i < formats.size(); ++i)
        folderFormat.addItem(formatName(formats[i]), i + 1);
    folderFormat.setSelectedId(1, juce::dontSendNotification);
    folderFormat.onChange = [this] {
        folderBox.updateContent();
        folderBox.repaint();
    };
    addChildComponent(folderFormat);
    addFolderButton.onClick = [this] { chooseFolder(); };
    removeFolderButton.setEnabled(false);
    removeFolderButton.onClick = [this] {
        const int defaults = getFolderLines().size() - folderModel->customCount();
        removeFolder(folderFormat.getText(), folderBox.getSelectedRow() - defaults);
    };
    addChildComponent(addFolderButton);
    addChildComponent(removeFolderButton);
#if JUCE_MAC
    folderNote.setText("Audio Units are listed by macOS itself: they need no folder.", juce::dontSendNotification);
#endif
    ThemedLookAndFeel::setFontRole(folderNote, appearance::FontRole::caption);
    ThemedLookAndFeel::setColourRole(folderNote, appearance::ColourRole::textSecondary);
    addChildComponent(folderNote);

    readLibrary();
    showPage(Page::plugins);
    setSize(780, 560);
    startTimerHz(4);
}

PluginManagerPanel::~PluginManagerPanel()
{
    for (auto* box : {&pluginBox, &notLoadedBox, &folderBox})
        box->setModel(nullptr);
}

void PluginManagerPanel::showPage(Page newPage)
{
    page = newPage;
    pageButtons[static_cast<std::size_t>(page)].setToggleState(true, juce::dontSendNotification);
    pluginBox.setVisible(page == Page::plugins);
    notLoadedBox.setVisible(page == Page::notLoaded);
    retryButton.setVisible(page == Page::notLoaded);
    notLoadedNote.setVisible(page == Page::notLoaded);
    for (juce::Component* part : std::initializer_list<juce::Component*>{&folderFormat, &folderBox, &addFolderButton,
                                                                         &removeFolderButton, &folderNote})
        part->setVisible(page == Page::folders);
    resized();
}

juce::StringArray PluginManagerPanel::getFolderLines() const
{
    juce::StringArray lines;
    const auto format = folderFormat.getText();
    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager(manager);
    for (auto* candidate : manager.getFormats())
        if (formatName(candidate->getName()) == format)
        {
            const auto defaults = controller::PluginCatalogue::defaultFolders(*candidate);
            for (int i = 0; i < defaults.getNumPaths(); ++i)
                lines.add(defaults.getRawString(i));
        }
    lines.addArray(library.getCatalogue().getCustomFolders(format));
    return lines;
}

void PluginManagerPanel::addFolder(const juce::String& format, const juce::File& folder)
{
    auto folders = library.getCatalogue().getCustomFolders(format);
    if (folders.contains(folder.getFullPathName()))
        return;
    folders.add(folder.getFullPathName());
    library.getCatalogue().setCustomFolders(format, folders);
    library.scan(false); // the new folder's plugins, and saves the catalogue
    folderBox.updateContent();
    folderBox.repaint();
}

void PluginManagerPanel::removeFolder(const juce::String& format, int index)
{
    auto folders = library.getCatalogue().getCustomFolders(format);
    if (index < 0 || index >= folders.size())
        return;
    folders.remove(index);
    library.getCatalogue().setCustomFolders(format, folders);
    library.scan(false); // forgets the plugins that were only there
    folderBox.updateContent();
    folderBox.deselectAllRows();
    folderBox.repaint();
}

void PluginManagerPanel::retry(int row)
{
    if (row < 0 || row >= static_cast<int>(blocked.size()))
        return;
    library.retry(blocked[static_cast<std::size_t>(row)].item);
    readLibrary();
}

void PluginManagerPanel::chooseFolder()
{
    const auto format = folderFormat.getText();
    chooser = std::make_unique<juce::FileChooser>("Choose a folder of " + format + " plugins");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this, format](const juce::FileChooser& finished) {
                             const auto folder = finished.getResult();
                             if (folder.isDirectory())
                                 addFolder(format, folder);
                         });
}

void PluginManagerPanel::readLibrary()
{
    shownRevision = library.getRevision();
    plugins = library.getCatalogue().getPlugins();
    blocked = library.getCatalogue().getBlocked();
    pageButtons[0].setButtonText("Plugins (" + juce::String(static_cast<int>(plugins.size())) + ")");
    pageButtons[1].setButtonText("Not Loaded (" + juce::String(static_cast<int>(blocked.size())) + ")");
    pluginBox.updateContent();
    notLoadedBox.updateContent();
    notLoadedBox.deselectAllRows();
    folderBox.updateContent();
    repaint();
}

void PluginManagerPanel::timerCallback()
{
    if (library.getRevision() != shownRevision)
        readLibrary();

    juce::String line;
    if (library.isScanning())
    {
        const auto state = library.getProgress();
        line = "Scanning " + juce::String(state.done) + " of " + juce::String(state.total);
        if (!state.current.isEmpty())
            line << ": " << juce::File(state.current[0]).getFileName();
    }
    else
    {
        line = juce::String(library.getCatalogue().getNumScannedItems()) + " items scanned";
    }
    if (progress.getText() != line)
        progress.setText(line, juce::dontSendNotification);
    cancel.setEnabled(library.isScanning());
}

void PluginManagerPanel::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::panel, false);
}

void PluginManagerPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    heading.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);

    auto scanRow = area.removeFromTop(26);
    scanNew.setBounds(scanRow.removeFromLeft(170));
    scanRow.removeFromLeft(6);
    scanAll.setBounds(scanRow.removeFromLeft(100));
    scanRow.removeFromLeft(6);
    cancel.setBounds(scanRow.removeFromLeft(80));
    scanRow.removeFromLeft(10);
    progress.setBounds(scanRow);
    area.removeFromTop(10);

    auto tabs = area.removeFromTop(26);
    for (auto& button : pageButtons)
    {
        button.setBounds(tabs.removeFromLeft(140));
        tabs.removeFromLeft(4);
    }
    area.removeFromTop(8);

    pluginBox.setBounds(area);

    auto notLoaded = area;
    auto bottom = notLoaded.removeFromBottom(26);
    retryButton.setBounds(bottom.removeFromRight(90));
    notLoadedNote.setBounds(bottom.withTrimmedRight(8));
    notLoaded.removeFromBottom(6);
    notLoadedBox.setBounds(notLoaded);

    auto folders = area;
    auto top = folders.removeFromTop(26);
    folderFormat.setBounds(top.removeFromLeft(120));
    top.removeFromLeft(10);
    folderNote.setBounds(top);
    folders.removeFromTop(6);
    auto buttons = folders.removeFromBottom(26);
    removeFolderButton.setBounds(buttons.removeFromRight(90));
    buttons.removeFromRight(6);
    addFolderButton.setBounds(buttons.removeFromRight(120));
    folders.removeFromBottom(6);
    folderBox.setBounds(folders);
}

} // namespace daw::ui::desktop
