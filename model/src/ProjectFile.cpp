#include "daw/model/ProjectFile.h"

#include "daw/model/FormatVersion.h"

#include <array>
#include <limits>

namespace daw::model
{

namespace
{

// The migration chain of this build: entry i upgrades format version i + 1 to i + 2.
// When the format changes, bump currentFormatVersion and append a migration.
constexpr std::array<FormatMigration, currentFormatVersion - 1> migrations{};
static_assert(migrations.size() + 1 == static_cast<std::size_t>(currentFormatVersion));

// JSON field names: the file format. Never rename one without a migration.
namespace json
{
constexpr const char* format = "format";
constexpr const char* formatVersion = "formatVersion";
constexpr const char* project = "project";
constexpr const char* loopEnabled = "loopEnabled";
constexpr const char* tracks = "tracks";
constexpr const char* name = "name";
constexpr const char* clips = "clips";
constexpr const char* audioFile = "audioFile";
} // namespace json

juce::var makeObject()
{
    return juce::var(new juce::DynamicObject());
}

juce::String pathForFile(const juce::File& file, const juce::File& projectDirectory)
{
    if (file == juce::File())
        return {};

    if (file.isAChildOf(projectDirectory))
        return file.getRelativePathFrom(projectDirectory).replaceCharacter('\\', '/');

    return file.getFullPathName();
}

juce::File fileForPath(const juce::String& path, const juce::File& projectDirectory)
{
    if (path.isEmpty())
        return {};

    if (juce::File::isAbsolutePath(path))
        return juce::File(path);

    return projectDirectory.getChildFile(path);
}

LoadedProject failure(int fileFormatVersion, const juce::String& message)
{
    LoadedProject result;
    result.fileFormatVersion = fileFormatVersion;
    result.error = message;
    return result;
}

LoadedProject damaged(int fileFormatVersion, const juce::String& detail)
{
    return failure(fileFormatVersion, "the project file is damaged (" + detail + ").");
}

std::optional<int> readVersion(const juce::var& value)
{
    if (!value.isInt() && !value.isInt64())
        return std::nullopt;

    const auto version = static_cast<juce::int64>(value);
    if (version < 1 || version > std::numeric_limits<int>::max())
        return std::nullopt;

    return static_cast<int>(version);
}

} // namespace

juce::String toJson(const Project& project, const juce::File& projectDirectory)
{
    juce::Array<juce::var> tracks;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        juce::Array<juce::var> clips;

        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = makeObject();
            clip.getDynamicObject()->setProperty(
                json::audioFile, pathForFile(track.getClip(c).getAudioFile(), projectDirectory));
            clips.add(clip);
        }

        auto trackObject = makeObject();
        trackObject.getDynamicObject()->setProperty(json::name, track.getName());
        trackObject.getDynamicObject()->setProperty(json::clips, clips);
        tracks.add(trackObject);
    }

    auto projectObject = makeObject();
    projectObject.getDynamicObject()->setProperty(json::loopEnabled, project.isLoopEnabled());
    projectObject.getDynamicObject()->setProperty(json::tracks, tracks);

    auto root = makeObject();
    root.getDynamicObject()->setProperty(json::format, projectFormatMarker);
    root.getDynamicObject()->setProperty(json::formatVersion, currentFormatVersion);
    root.getDynamicObject()->setProperty(json::project, projectObject);

    // JUCE lays JSON out with CRLF; project files use LF on every platform so that
    // they are byte-identical everywhere. (Line breaks inside strings are escaped.)
    const auto text = juce::JSON::toString(
        root, juce::JSON::FormatOptions{}.withSpacing(juce::JSON::Spacing::multiLine));
    return text.replace("\r\n", "\n") + "\n";
}

LoadedProject fromJson(const juce::String& text, const juce::File& projectDirectory)
{
    return detail::fromJson(text, projectDirectory, migrations);
}

LoadedProject detail::fromJson(const juce::String& text, const juce::File& projectDirectory,
                               std::span<const FormatMigration> migrationChain)
{
    const int newestVersion = static_cast<int>(migrationChain.size()) + 1;

    juce::var root;
    if (juce::JSON::parse(text, root).failed() || !root.isObject())
        return failure(0, "the file is not a project file (it is not valid JSON).");

    if (root[json::format].toString() != projectFormatMarker)
        return failure(0, "the file is not a project file.");

    const auto version = readVersion(root[json::formatVersion]);
    if (!version)
        return failure(0, "the file has no valid format version.");

    if (*version > newestVersion)
        return failure(*version, "the project was saved by a newer version of the app (file format " +
                                     juce::String(*version) + "; this version reads up to " +
                                     juce::String(newestVersion) +
                                     "). Update the app to open it.");

    for (int from = *version; from < newestVersion; ++from)
    {
        root = migrationChain[static_cast<std::size_t>(from - 1)](root);
        if (!root.isObject())
            return damaged(*version, "upgrading from format " + juce::String(from) + " failed");
        root.getDynamicObject()->setProperty(json::formatVersion, from + 1);
    }

    const auto& projectJson = root[json::project];
    if (!projectJson.isObject())
        return damaged(*version, "no project section");

    Project project;

    const auto& loop = projectJson[json::loopEnabled];
    if (!loop.isVoid() && !loop.isBool())
        return damaged(*version, "loopEnabled is not true or false");
    project.setLoopEnabled(static_cast<bool>(loop));

    const auto& tracks = projectJson[json::tracks];
    if (!tracks.isVoid() && !tracks.isArray())
        return damaged(*version, "tracks is not a list");

    const auto* trackList = tracks.getArray();
    for (const auto& trackJson : trackList != nullptr ? *trackList : juce::Array<juce::var>{})
    {
        if (!trackJson.isObject())
            return damaged(*version, "a track is not an object");

        const auto& name = trackJson[json::name];
        if (!name.isVoid() && !name.isString())
            return damaged(*version, "a track name is not text");

        auto track = project.addTrack(name.toString());

        const auto& clips = trackJson[json::clips];
        if (!clips.isVoid() && !clips.isArray())
            return damaged(*version, "the clips of a track are not a list");

        const auto* clipList = clips.getArray();
        if (clipList == nullptr)
            continue;

        for (const auto& clipJson : *clipList)
        {
            const auto& audioFile = clipJson[json::audioFile];
            if (!clipJson.isObject() || !audioFile.isString())
                return damaged(*version, "a clip has no audio file");

            track.addClip(fileForPath(audioFile.toString(), projectDirectory));
        }
    }

    LoadedProject result;
    result.project = std::move(project);
    result.fileFormatVersion = *version;
    return result;
}

juce::Result saveProject(const Project& project, const juce::File& file)
{
    const auto failed = [&file](const juce::String& reason) {
        return juce::Result::fail("Could not save \"" + file.getFileName() + "\": " + reason);
    };

    if (!file.getParentDirectory().isDirectory())
        return failed("its folder does not exist.");

    const juce::TemporaryFile temporary(file);
    // UTF-8, no byte-order mark, LF line endings.
    if (!temporary.getFile().replaceWithText(toJson(project, file.getParentDirectory()), false,
                                             false, "\n"))
        return failed("the file could not be written.");

    if (!temporary.overwriteTargetFileWithTemporary())
        return failed("the existing file could not be replaced.");

    return juce::Result::ok();
}

LoadedProject loadProject(const juce::File& file)
{
    const auto prefix = "Could not open \"" + file.getFileName() + "\": ";

    if (!file.existsAsFile())
        return failure(0, prefix + "the file does not exist.");

    juce::FileInputStream stream(file);
    if (!stream.openedOk())
        return failure(0, prefix + "the file could not be read.");

    auto result = fromJson(stream.readEntireStreamAsString(), file.getParentDirectory());
    if (!result.succeeded())
        result.error = prefix + result.error;

    return result;
}

} // namespace daw::model
