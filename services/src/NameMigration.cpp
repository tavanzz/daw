#include "daw/services/NameMigration.h"

namespace daw::services
{

namespace
{

constexpr const char* settingsSuffix = ".settings";

// Crash reports, as AppController names their folders: those waiting to be sent, in the
// settings folder (sentry's database and the outbox), and those kept, in the log folder.
constexpr const char* crashDatabaseFolder = "Crash Reporting";
constexpr const char* crashReportsFolder = "Crashes";

juce::File settingsFile(const NameMigration::Folders& folders)
{
    return folders.settings.getChildFile(folders.name + settingsSuffix);
}

juce::Array<juce::File> childrenOf(const juce::File& folder, int types, const juce::String& pattern = "*")
{
    return folder.findChildFiles(types, false, pattern, juce::File::FollowSymlinks::no);
}

// Copies a file or a folder; whatever is already at the target stays as it is.
bool copyKeeping(const juce::File& source, const juce::File& target)
{
    if (!source.isDirectory())
        return target.exists() || source.copyFileTo(target);

    if (!target.createDirectory().wasOk())
        return false;
    bool copied = true;
    for (const auto& child : childrenOf(source, juce::File::findFilesAndDirectories))
        copied = copyKeeping(child, target.getChildFile(child.getFileName())) && copied;
    return copied;
}

// Moves a file or a folder unless something is already at the target.
bool moveUnlessThere(const juce::File& source, const juce::File& target)
{
    return !target.exists() && target.getParentDirectory().createDirectory().wasOk() && source.moveFileTo(target);
}

} // namespace

NameMigration::Result NameMigration::run(const Folders& previous, const Folders& current)
{
    Result result;
    const auto previousSettings = settingsFile(previous);
    const auto currentSettings = settingsFile(current);
    if (previous.name == current.name || currentSettings.exists() || !previousSettings.existsAsFile())
        return result;

    if (!current.settings.createDirectory().wasOk() || !previousSettings.copyFileTo(currentSettings))
    {
        result.problems.add("could not copy " + previousSettings.getFullPathName());
        return result;
    }
    result.tookOver = true;

    // What else lives with the settings (the plugin catalogue, the crash marker, ...) is
    // copied; reports waiting to be sent move, so that only one copy of the app sends them.
    if (previous.settings != current.settings)
        for (const auto& child : childrenOf(previous.settings, juce::File::findFilesAndDirectories))
        {
            const auto target = current.settings.getChildFile(child.getFileName());
            if (child == previousSettings || child == previous.logs)
                continue;
            const bool done = child.getFileName() == crashDatabaseFolder ? moveUnlessThere(child, target)
                                                                         : copyKeeping(child, target);
            if (!done)
                result.problems.add("could not take over " + child.getFullPathName());
        }

    // Session logs take the new name, so that rotation counts them with the new ones.
    const juce::String prefix = previous.name + " ";
    for (const auto& file : childrenOf(previous.logs, juce::File::findFiles, prefix + "*.log"))
    {
        const auto target = current.logs.getChildFile(current.name + " " + file.getFileName().substring(prefix.length()));
        if (moveUnlessThere(file, target))
            ++result.logsMoved;
        else
            result.problems.add("could not move " + file.getFullPathName());
    }

    if (previous.logs != current.logs)
    {
        const auto reports = previous.logs.getChildFile(crashReportsFolder);
        for (const auto& file : childrenOf(reports, juce::File::findFiles))
        {
            if (moveUnlessThere(file, current.logs.getChildFile(crashReportsFolder).getChildFile(file.getFileName())))
                ++result.crashReportsMoved;
            else
                result.problems.add("could not move " + file.getFullPathName());
        }

        // Folders left empty go (deleteFile() removes a folder only when it is empty).
        reports.deleteFile();
        previous.logs.deleteFile();
    }
    return result;
}

juce::String NameMigration::Result::describe(const Folders& from) const
{
    if (!tookOver)
        return problems.isEmpty() ? juce::String() : "Could not take over " + from.name + "'s settings: " +
                                                         problems.joinIntoString("; ");

    const auto count = [](int number, const juce::String& what)
    { return juce::String(number) + " " + what + (number == 1 ? "" : "s"); };
    auto line = "Took over the settings of " + from.name + " (the app's previous name) from " +
                from.settings.getFullPathName() + ", " + count(logsMoved, "session log") + " and " +
                count(crashReportsMoved, "crash report");
    if (!problems.isEmpty())
        line << "; " << problems.joinIntoString("; ");
    return line;
}

} // namespace daw::services
