#include "daw/controller/RecordingJournal.h"

#include "daw/controller/RecordingController.h"

namespace daw::controller
{

namespace
{

constexpr const char* formatMarker = "daw-recording-journal";

namespace json
{
constexpr const char* format = "format";
constexpr const char* recordings = "recordings";
constexpr const char* file = "file";
constexpr const char* track = "track";
constexpr const char* firstPosition = "firstPosition";
constexpr const char* keepStart = "keepStart";
constexpr const char* keepEnd = "keepEnd";
} // namespace json

} // namespace

juce::File RecordingJournal::fileFor(const juce::File& projectDirectory)
{
    return projectDirectory.getChildFile(recordingsFolderName).getChildFile(".recording-journal.json");
}

std::vector<JournalEntry> RecordingJournal::read(const juce::File& projectDirectory)
{
    std::vector<JournalEntry> entries;
    const auto file = fileFor(projectDirectory);
    if (!file.existsAsFile())
        return entries;

    const auto root = juce::JSON::parse(file.loadFileAsString());
    if (root[json::format].toString() != formatMarker)
        return entries;

    const auto isInteger = [](const juce::var& value) { return value.isInt() || value.isInt64(); };

    if (const auto* list = root[json::recordings].getArray())
    {
        for (const auto& item : *list)
        {
            const auto& path = item[json::file];
            if (!path.isString() || !isInteger(item[json::track]) ||
                !isInteger(item[json::firstPosition]) || !isInteger(item[json::keepStart]))
                continue;

            JournalEntry entry;
            entry.file = projectDirectory.getChildFile(path.toString());
            entry.trackId = static_cast<juce::int64>(item[json::track]);
            entry.firstPosition = static_cast<model::Ticks>(item[json::firstPosition]);
            entry.keepStart = static_cast<model::Ticks>(item[json::keepStart]);
            if (isInteger(item[json::keepEnd]))
                entry.keepEnd = static_cast<model::Ticks>(item[json::keepEnd]);
            entries.push_back(entry);
        }
    }

    return entries;
}

bool RecordingJournal::write(const juce::File& projectDirectory,
                             const std::vector<JournalEntry>& entries)
{
    const auto file = fileFor(projectDirectory);
    if (entries.empty())
        return !file.exists() || file.deleteFile();

    juce::Array<juce::var> list;
    for (const auto& entry : entries)
    {
        auto* item = new juce::DynamicObject();
        item->setProperty(json::file,
                          entry.file.getRelativePathFrom(projectDirectory).replaceCharacter('\\', '/'));
        item->setProperty(json::track, entry.trackId);
        item->setProperty(json::firstPosition, entry.firstPosition);
        item->setProperty(json::keepStart, entry.keepStart);
        if (entry.keepEnd)
            item->setProperty(json::keepEnd, *entry.keepEnd);
        list.add(juce::var(item));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty(json::format, formatMarker);
    root->setProperty(json::recordings, list);

    if (!file.getParentDirectory().createDirectory())
        return false;

    // Replaced in one go: a crash while writing it must not lose the old one.
    const juce::TemporaryFile temporary(file);
    return temporary.getFile().replaceWithText(juce::JSON::toString(juce::var(root)), false, false,
                                               "\n") &&
           temporary.overwriteTargetFileWithTemporary();
}

} // namespace daw::controller
