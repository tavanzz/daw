#pragma once

#include "daw/controller/PluginCatalogue.h"
#include "daw/controller/PluginScanner.h"

#include <functional>
#include <memory>

namespace daw::controller
{

/**
    The app's plugins: the catalogue, kept in a file, and the scanner that fills it in
    separate processes. scan() plans what is new or changed (PluginCatalogue::planScan())
    and queues it; update(), on a UI timer, puts the results in the catalogue (failures
    on the blacklist) and saves it when it changed. Message thread.
*/
class PluginLibrary
{
public:
    struct Options
    {
        juce::File catalogueFile; ///< read at start, written after every change
        PluginScanner::Options scanner;
        /** Adds the formats to plan with (default: JUCE's headless formats); tests use
            their own. */
        std::function<void(juce::AudioPluginFormatManager&)> addFormats;
    };

    explicit PluginLibrary(Options options);

    [[nodiscard]] PluginCatalogue& getCatalogue() noexcept { return catalogue; }
    [[nodiscard]] const PluginCatalogue& getCatalogue() const noexcept { return catalogue; }

    /** Queues what is new or changed; everything: every item but the blacklisted. */
    void scan(bool everything = false);

    /** Takes an item off the blacklist and scans it again. */
    void retry(const ScanItem& item);

    void cancelScan();

    /** Applies the scanner's results; true if the catalogue changed (then saved). */
    bool update();

    [[nodiscard]] PluginScanner::Progress getProgress() const { return scanner.getProgress(); }
    [[nodiscard]] bool isScanning() const { return scanner.isScanning(); }

    /** Tests: scans and waits until the results are in the catalogue. */
    bool scanAndWait(bool everything, int timeoutMilliseconds);

private:
    [[nodiscard]] std::map<juce::String, juce::StringArray> scannerFolders();

    Options options;
    juce::AudioPluginFormatManager formats;
    PluginCatalogue catalogue;
    PluginScanner scanner;
};

} // namespace daw::controller
