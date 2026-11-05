#include "daw/controller/PluginScanner.h"

#include "daw/engine/plugins/PluginLayout.h"

#include <juce_events/juce_events.h>

#include <deque>
#include <functional>

namespace daw::controller
{

namespace
{

constexpr double scanSampleRate = 48000.0;
constexpr int scanBlockSize = 512;

juce::AudioPluginFormat* findFormat(juce::AudioPluginFormatManager& formats, const juce::String& name)
{
    for (auto* format : formats.getFormats())
        if (format->getName() == name)
            return format;
    return nullptr;
}

// What one plugin of the item gives once loaded, or the reason it did not load.
PluginInfo describeLoaded(const juce::PluginDescription& found, juce::AudioPluginInstance& instance)
{
    PluginInfo info;
    info.description = found;
    const auto layout = engine::plugins::setUpStereoLayout(instance);
    if (const auto* reason = std::get_if<juce::String>(&layout))
    {
        info.unsupported = *reason;
    }
    else
    {
        const auto channels = std::get<engine::plugins::PluginLayout>(layout);
        info.description.numInputChannels = channels.inputs;
        info.description.numOutputChannels = channels.outputs;
    }
    return info;
}

// Loads the item's plugins one after the other; done gets the outcome on the message
// thread. Formats that instantiate asynchronously (AUv3) answer later, the others at
// once.
class ItemScan : public std::enable_shared_from_this<ItemScan>
{
public:
    ItemScan(juce::AudioPluginFormatManager& formatsToUse, juce::AudioPluginFormat& formatToUse,
             std::function<void(ScanOutcome)> whenDone)
        : formats(formatsToUse)
        , format(formatToUse)
        , done(std::move(whenDone))
    {
    }

    void start(const ScanItem& item)
    {
        format.findAllTypesForFile(found, item.item);
        if (found.isEmpty())
        {
            outcome.failure = ScanFailure::noPlugins;
            finish();
            return;
        }
        next();
    }

private:
    void next()
    {
        if (index >= found.size())
        {
            if (outcome.plugins.empty())
            {
                outcome.failure = ScanFailure::failedToLoad;
                outcome.detail = errors.joinIntoString("; ");
            }
            finish();
            return;
        }

        const auto& description = *found[index];
        if (format.requiresUnblockedMessageThreadDuringCreation(description))
        {
            auto self = shared_from_this();
            format.createPluginInstanceAsync(
                description, scanSampleRate, scanBlockSize,
                [self](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
                    self->loaded(std::move(instance), error);
                });
            return;
        }

        juce::String error;
        auto instance = format.createInstanceFromDescription(description, scanSampleRate, scanBlockSize, error);
        loaded(std::move(instance), error);
    }

    void loaded(std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
    {
        const auto& description = *found[index];
        if (instance != nullptr)
            outcome.plugins.push_back(describeLoaded(description, *instance));
        else
            errors.add(description.name + ": " + (error.isNotEmpty() ? error : juce::String("did not load")));
        instance.reset();
        ++index;
        next();
    }

    void finish()
    {
        juce::ignoreUnused(formats);
        if (done)
            std::exchange(done, nullptr)(std::move(outcome));
    }

    juce::AudioPluginFormatManager& formats;
    juce::AudioPluginFormat& format;
    std::function<void(ScanOutcome)> done;
    juce::OwnedArray<juce::PluginDescription> found;
    int index = 0;
    ScanOutcome outcome;
    juce::StringArray errors;
};

void scanItemAsync(juce::AudioPluginFormatManager& formats, const ScanItem& item,
                   const juce::StringArray& folders, std::function<void(ScanOutcome)> done)
{
    auto* format = findFormat(formats, item.format);
    if (format == nullptr)
    {
        ScanOutcome outcome;
        outcome.failure = ScanFailure::failedToLoad;
        outcome.detail = "unknown format " + item.format;
        done(std::move(outcome));
        return;
    }

    // LV2 knows only the plugins of the folders it has read.
    if (!folders.isEmpty())
    {
        juce::FileSearchPath path;
        for (const auto& folder : folders)
            path.add(juce::File(folder));
        [[maybe_unused]] const auto ignored = format->searchPathsForPlugins(path, true, true);
    }

    std::make_shared<ItemScan>(formats, *format, std::move(done))->start(item);
}

juce::StringArray valuesOf(const juce::StringArray& arguments, const juce::String& name)
{
    juce::StringArray values;
    for (int i = 0; i + 1 < arguments.size(); ++i)
        if (arguments[i] == name)
            values.add(arguments[i + 1]);
    return values;
}

} // namespace

ScanOutcome scanItem(juce::AudioPluginFormatManager& formats, const ScanItem& item,
                     const juce::StringArray& folders)
{
    std::optional<ScanOutcome> result;
    scanItemAsync(formats, item, folders, [&](ScanOutcome outcome) { result = std::move(outcome); });
    if (!result)
    {
        // Only a format that instantiates asynchronously gets here: this function is
        // for those that do not (the scanner process runs a message loop instead).
        ScanOutcome outcome;
        outcome.failure = ScanFailure::failedToLoad;
        outcome.detail = "needs the message loop to load";
        return outcome;
    }
    return std::move(*result);
}

juce::StringArray scannerArguments(const ScanItem& item, const juce::File& result,
                                   const juce::StringArray& folders)
{
    juce::StringArray arguments{"--format", item.format, "--item", item.item, "--result",
                                result.getFullPathName()};
    for (const auto& folder : folders)
        arguments.addArray({"--folder", folder});
    return arguments;
}

juce::var outcomeToJson(const ScanOutcome& outcome)
{
    auto* object = new juce::DynamicObject();
    juce::Array<juce::var> plugins;
    for (const auto& info : outcome.plugins)
    {
        auto plugin = descriptionToJson(info.description);
        if (info.unsupported.isNotEmpty())
            plugin.getDynamicObject()->setProperty("unsupported", info.unsupported);
        plugins.add(plugin);
    }
    object->setProperty("plugins", plugins);
    if (outcome.failure)
        object->setProperty("failure", toString(*outcome.failure));
    if (outcome.detail.isNotEmpty())
        object->setProperty("detail", outcome.detail);
    object->setProperty("complete", true);
    return juce::var(object);
}

std::optional<ScanOutcome> outcomeFromJson(const juce::var& json)
{
    if (!json.isObject() || !static_cast<bool>(json["complete"]))
        return std::nullopt;
    ScanOutcome outcome;
    if (const auto* plugins = json["plugins"].getArray())
        for (const auto& plugin : *plugins)
            if (auto description = descriptionFromJson(plugin))
                outcome.plugins.push_back({std::move(*description), plugin["unsupported"].toString()});
    if (json.hasProperty("failure"))
    {
        outcome.failure = scanFailureFromString(json["failure"].toString());
        if (!outcome.failure)
            return std::nullopt;
    }
    outcome.detail = json["detail"].toString();
    return outcome;
}

bool runScannerProcess(const juce::StringArray& arguments, std::function<void(int)> finished)
{
    const auto formatArgument = valuesOf(arguments, "--format");
    const auto itemArgument = valuesOf(arguments, "--item");
    const auto resultArgument = valuesOf(arguments, "--result");
    if (formatArgument.size() != 1 || itemArgument.size() != 1 || resultArgument.size() != 1)
        return false;

    struct Scan
    {
        juce::AudioPluginFormatManager formats;
        ScanItem item;
        juce::File result;
        juce::StringArray folders;
    };
    auto scan = std::make_shared<Scan>();
    juce::addHeadlessDefaultFormatsToManager(scan->formats);
    scan->item = {formatArgument[0], itemArgument[0]};
    scan->result = juce::File(resultArgument[0]);
    scan->folders = valuesOf(arguments, "--folder");

    // From the message loop, which AUv3 plugins need to answer.
    juce::MessageManager::callAsync([scan, whenDone = std::move(finished)]() mutable {
        scanItemAsync(scan->formats, scan->item, scan->folders,
                      [scan, done = std::move(whenDone)](ScanOutcome outcome) mutable {
                          const bool written = scan->result.replaceWithText(
                              juce::JSON::toString(outcomeToJson(outcome)), false, false, "\n");
                          done(written ? 0 : 74); // EX_IOERR
                      });
    });
    return true;
}

// The thread that runs the scanner processes.
class PluginScanner::Worker final : public juce::Thread
{
public:
    explicit Worker(Options newOptions)
        : Thread("Plugin scanner")
        , options(std::move(newOptions))
    {
        resultsFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("daw-plugin-scan", "", false);
        resultsFolder.createDirectory();
        startThread();
    }

    ~Worker() override
    {
        signalThreadShouldExit();
        notify();
        stopThread(10000);
        resultsFolder.deleteRecursively();
    }

    void queueItems(const std::vector<ScanItem>& items, const std::map<juce::String, juce::StringArray>& folders)
    {
        {
            const std::scoped_lock lock(mutex);
            for (const auto& item : items)
            {
                const auto found = folders.find(item.format);
                queue.push_back({item, found != folders.end() ? found->second : juce::StringArray{}});
            }
            total += static_cast<int>(items.size());
        }
        notify();
    }

    void cancelAll()
    {
        {
            const std::scoped_lock lock(mutex);
            total -= static_cast<int>(queue.size());
            queue.clear();
            cancelling = true;
        }
        notify();
    }

    std::vector<Result> takeResults()
    {
        const std::scoped_lock lock(mutex);
        return std::exchange(results, {});
    }

    Progress getProgress() const
    {
        const std::scoped_lock lock(mutex);
        return {done, total, current};
    }

    bool isBusy() const
    {
        const std::scoped_lock lock(mutex);
        return !queue.empty() || !current.isEmpty();
    }

private:
    struct Request
    {
        ScanItem item;
        juce::StringArray folders;
    };

    struct Running
    {
        ScanItem item;
        std::unique_ptr<juce::ChildProcess> process;
        juce::File result;
        juce::uint32 started = 0;
    };

    void run() override
    {
        while (!threadShouldExit())
        {
            tick();
            // Asleep only with nothing running and nothing queued (scan() wakes it).
            bool idle = running.empty();
            if (idle)
            {
                const std::scoped_lock lock(mutex);
                idle = queue.empty() && !cancelling;
            }
            wait(idle ? -1 : 10);
        }
        for (auto& job : running)
            job.process->kill();
        running.clear();
    }

    void tick()
    {
        bool cancel = false;
        {
            const std::scoped_lock lock(mutex);
            cancel = std::exchange(cancelling, false);
        }
        if (cancel)
        {
            for (auto& job : running)
                job.process->kill();
            const std::scoped_lock lock(mutex);
            total -= static_cast<int>(running.size());
            running.clear();
            current.clear();
        }

        // Start what there is room for (current at once, so that the scanner never
        // looks idle between taking an item and starting its process).
        while (static_cast<int>(running.size()) < std::max(1, options.processes))
        {
            Request request;
            {
                const std::scoped_lock lock(mutex);
                if (queue.empty())
                    break;
                request = std::move(queue.front());
                queue.pop_front();
                current.add(request.item.item);
            }
            launch(request);
        }

        // Collect what finished or ran out of time.
        for (auto it = running.begin(); it != running.end();)
        {
            ScanOutcome outcome;
            const auto elapsed = juce::Time::getMillisecondCounter() - it->started;
            if (it->process->isRunning())
            {
                if (static_cast<int>(elapsed) < options.timeoutMilliseconds)
                {
                    ++it;
                    continue;
                }
                it->process->kill();
                outcome.failure = ScanFailure::timedOut;
                outcome.detail = "no answer in " + juce::String(options.timeoutMilliseconds / 1000) + " s";
            }
            else
            {
                const auto read = outcomeFromJson(juce::JSON::parse(it->result));
                if (read)
                {
                    outcome = *read;
                }
                else
                {
                    outcome.failure = ScanFailure::crashed;
                    outcome.detail = "the scanner stopped (exit code " +
                                     juce::String(it->process->getExitCode()) + ")";
                }
            }
            it->result.deleteFile();
            finished(it->item, std::move(outcome));
            it = running.erase(it);
        }
    }

    void launch(const Request& request)
    {
        Running job;
        job.item = request.item;
        // A name of its own (the file only exists once the scanner writes it).
        job.result = resultsFolder.getChildFile("result-" + juce::String(++launched) + ".json");
        job.result.deleteFile();
        job.process = std::make_unique<juce::ChildProcess>();
        auto arguments = scannerArguments(request.item, job.result, request.folders);
        arguments.insert(0, options.executable.getFullPathName());
        job.started = juce::Time::getMillisecondCounter();
        // No output streams: a plugin printing a lot must never block on a full pipe.
        if (!job.process->start(arguments, 0))
        {
            ScanOutcome outcome;
            outcome.failure = ScanFailure::failedToLoad;
            outcome.detail = "the scanner could not start";
            finished(request.item, std::move(outcome));
            return;
        }
        running.push_back(std::move(job));
    }

    void finished(const ScanItem& item, ScanOutcome outcome)
    {
        const std::scoped_lock lock(mutex);
        current.removeString(item.item);
        ++done;
        results.push_back({item, std::move(outcome)});
    }

    Options options;
    juce::File resultsFolder;
    std::vector<Running> running; // this thread only
    juce::int64 launched = 0;     // this thread only

    mutable std::mutex mutex; // guards what follows
    std::deque<Request> queue;
    std::vector<Result> results;
    juce::StringArray current;
    int done = 0;
    int total = 0;
    bool cancelling = false;
};

PluginScanner::PluginScanner(Options options)
    : worker(std::make_unique<Worker>(std::move(options)))
{
}

PluginScanner::~PluginScanner() = default;

void PluginScanner::scan(const std::vector<ScanItem>& items,
                         const std::map<juce::String, juce::StringArray>& folders)
{
    worker->queueItems(items, folders);
}

void PluginScanner::cancel()
{
    worker->cancelAll();
}

std::vector<PluginScanner::Result> PluginScanner::takeResults()
{
    return worker->takeResults();
}

PluginScanner::Progress PluginScanner::getProgress() const
{
    return worker->getProgress();
}

bool PluginScanner::isScanning() const
{
    return worker->isBusy();
}

bool PluginScanner::waitUntilDone(int timeoutMilliseconds)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMilliseconds);
    while (worker->isBusy())
    {
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        juce::Thread::sleep(10);
    }
    return true;
}

} // namespace daw::controller
