#pragma once

#include "daw/controller/PluginCatalogue.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace daw::controller
{

/** What scanning one item gave: its plugins, or why it failed. */
struct ScanOutcome
{
    std::vector<PluginInfo> plugins;
    std::optional<ScanFailure> failure;
    juce::String detail;
};

/**
    The scanner process's work, for one item: loads it with the format (for LV2,
    after reading the folders), lists its plugins, instantiates each once to check it
    loads and find what layout it can have (engine::plugins::setUpStereoLayout()),
    and releases it. This is what may crash or hang: only the scanner process calls
    it (and tests, with plugins that behave). Message thread of that process.
*/
[[nodiscard]] ScanOutcome scanItem(juce::AudioPluginFormatManager& formats, const ScanItem& item,
                                   const juce::StringArray& folders);

/** The scanner process's arguments for an item, and back. */
[[nodiscard]] juce::StringArray scannerArguments(const ScanItem& item, const juce::File& result,
                                                 const juce::StringArray& folders);

/**
    The scanner process's work, started from its JUCE application: reads its
    arguments ("--format", "--item", "--result", "--folder"...), scans the item with
    JUCE's headless formats inside the message loop and writes the outcome as JSON
    into the result file, last of all, with "complete": true (a process that ends
    without that file complete crashed), then calls finished with the exit code.
    False if the arguments are wrong.
*/
bool runScannerProcess(const juce::StringArray& arguments, std::function<void(int)> finished);

/** The scanner's JSON, and back. */
[[nodiscard]] juce::var outcomeToJson(const ScanOutcome& outcome);
[[nodiscard]] std::optional<ScanOutcome> outcomeFromJson(const juce::var& json);

/**
    Scans items in separate processes, so that a plugin that crashes or hangs never
    takes the app with it: one process per item, a few at once, each given a time
    limit. A process that dies without its result is a crash, one that runs out of
    time is killed (a hang); both go to the blacklist with the caller's catalogue.

    Its own thread starts and watches the processes; the message thread queues items
    (scan()) and takes the results (takeResults()) to put them in the catalogue.
*/
class PluginScanner
{
public:
    struct Options
    {
        juce::File executable; ///< the scanner program (runScannerProcess())
        int processes = 4;     ///< at once
        int timeoutMilliseconds = 30000;
    };

    struct Result
    {
        ScanItem item;
        ScanOutcome outcome;
    };

    struct Progress
    {
        int done = 0;
        int total = 0;
        juce::StringArray current; ///< items being scanned now
    };

    explicit PluginScanner(Options options);
    ~PluginScanner();

    PluginScanner(const PluginScanner&) = delete;
    PluginScanner& operator=(const PluginScanner&) = delete;

    /** Message thread. Queues items (after those queued already); folders: per format,
        the folders the scanner process must read first (LV2). */
    void scan(const std::vector<ScanItem>& items,
              const std::map<juce::String, juce::StringArray>& folders = {});

    /** Message thread. Forgets what is queued and kills what runs. */
    void cancel();

    /** Message thread. The results since the last call. */
    [[nodiscard]] std::vector<Result> takeResults();

    [[nodiscard]] Progress getProgress() const;
    [[nodiscard]] bool isScanning() const;

    /** Message thread, mostly for tests: waits until nothing is queued or running. */
    bool waitUntilDone(int timeoutMilliseconds);

private:
    class Worker;
    std::unique_ptr<Worker> worker;
};

} // namespace daw::controller
