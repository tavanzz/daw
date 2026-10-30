#include "daw/model/ProjectFile.h"

#include "daw/model/FormatVersion.h"

#include "Identifiers.h"
#include "ObjectIds.h"

#include <array>
#include <limits>
#include <set>

namespace daw::model
{

namespace
{

// JSON field names: the file format. Never rename one without a migration.
namespace json
{
constexpr const char* format = "format";
constexpr const char* formatVersion = "formatVersion";
constexpr const char* project = "project";
constexpr const char* tempo = "tempo";
constexpr const char* timeSignature = "timeSignature";
constexpr const char* numerator = "numerator";
constexpr const char* denominator = "denominator";
constexpr const char* loop = "loop";
constexpr const char* enabled = "enabled";
constexpr const char* start = "start";
constexpr const char* end = "end";
constexpr const char* snap = "snap";
constexpr const char* grid = "grid";
constexpr const char* master = "master";
constexpr const char* volume = "volume";
constexpr const char* tracks = "tracks";
constexpr const char* id = "id";
constexpr const char* name = "name";
constexpr const char* pan = "pan";
constexpr const char* mute = "mute";
constexpr const char* solo = "solo";
constexpr const char* clips = "clips";
constexpr const char* audioFile = "audioFile";
constexpr const char* position = "position";
constexpr const char* offset = "offset";
constexpr const char* length = "length";

// Fields of older versions, read only by migrations.
constexpr const char* loopEnabledVersion1 = "loopEnabled";
} // namespace json

juce::var makeObject()
{
    return juce::var(new juce::DynamicObject());
}

juce::var makeObject(std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto object = makeObject();
    for (const auto& [name, value] : properties)
        object.getDynamicObject()->setProperty(name, value);
    return object;
}

// Version 2 (Phase 2, multitrack) adds tempo, metre, a loop region, snap settings,
// the master bus, track mixer settings, clip placement and ids. Version 1 clips were
// whole files at the start of the timeline. Anything unexpected is left as it is,
// for the reader to report.
juce::var migrateVersion1To2(juce::var root)
{
    auto* project = root[json::project].getDynamicObject();
    if (project == nullptr)
        return root;

    project->setProperty(json::tempo, TempoMap::defaultTempo);
    project->setProperty(json::timeSignature,
                         makeObject({{json::numerator, 4}, {json::denominator, 4}}));
    project->setProperty(json::loop, makeObject({{json::enabled,
                                                  project->getProperty(json::loopEnabledVersion1)},
                                                 {json::start, 0},
                                                 {json::end, 0}}));
    project->removeProperty(json::loopEnabledVersion1);
    project->setProperty(json::snap, makeObject({{json::enabled, true}, {json::grid, "auto"}}));
    project->setProperty(json::master, makeObject({{json::volume, 1.0}}));

    int nextId = 1;
    auto* tracks = project->getProperty(json::tracks).getArray();
    if (tracks == nullptr)
        return root;

    for (auto& track : *tracks)
    {
        auto* trackObject = track.getDynamicObject();
        if (trackObject == nullptr)
            continue;

        trackObject->setProperty(json::id, nextId++);
        trackObject->setProperty(json::volume, 1.0);
        trackObject->setProperty(json::pan, 0.0);
        trackObject->setProperty(json::mute, false);
        trackObject->setProperty(json::solo, false);

        auto* clips = trackObject->getProperty(json::clips).getArray();
        if (clips == nullptr)
            continue;

        for (auto& clip : *clips)
        {
            if (auto* clipObject = clip.getDynamicObject())
            {
                clipObject->setProperty(json::id, nextId++);
                clipObject->setProperty(json::position, 0);
                clipObject->setProperty(json::offset, 0);
            }
        }
    }

    return root;
}

// The migration chain of this build: entry i upgrades format version i + 1 to i + 2.
// When the format changes, bump currentFormatVersion and append a migration.
constexpr std::array<FormatMigration, currentFormatVersion - 1> migrations{migrateVersion1To2};
static_assert(migrations.size() + 1 == static_cast<std::size_t>(currentFormatVersion));

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
            const auto clip = track.getClip(c);
            auto clipObject = makeObject({
                {json::id, clip.getId()},
                {json::audioFile, pathForFile(clip.getAudioFile(), projectDirectory)},
                {json::position, clip.getPosition()},
                {json::offset, clip.getOffset()},
            });
            if (const auto length = clip.getLength())
                clipObject.getDynamicObject()->setProperty(json::length, *length);
            clips.add(clipObject);
        }

        tracks.add(makeObject({
            {json::id, track.getId()},
            {json::name, track.getName()},
            {json::volume, track.getVolume()},
            {json::pan, track.getPan()},
            {json::mute, track.isMuted()},
            {json::solo, track.isSoloed()},
            {json::clips, clips},
        }));
    }

    const auto tempoMap = project.getTempoMap();
    const auto signature = tempoMap.getTimeSignature();

    const auto projectObject = makeObject({
        {json::tempo, tempoMap.getTempo()},
        {json::timeSignature, makeObject({{json::numerator, signature.numerator},
                                          {json::denominator, signature.denominator}})},
        {json::loop, makeObject({{json::enabled, project.isLoopEnabled()},
                                 {json::start, project.getLoopStart()},
                                 {json::end, project.getLoopEnd()}})},
        {json::snap, makeObject({{json::enabled, project.isSnapEnabled()},
                                 {json::grid, toString(project.getGridDivision())}})},
        {json::master, makeObject({{json::volume, project.getMasterVolume()}})},
        {json::tracks, tracks},
    });

    const auto root = makeObject({
        {json::format, projectFormatMarker},
        {json::formatVersion, currentFormatVersion},
        {json::project, projectObject},
    });

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

namespace
{

// Typed access to optional fields: a missing field gives the fallback, a field of
// another type gives nothing (the file is damaged).
std::optional<bool> readBool(const juce::var& object, const char* name, bool fallback)
{
    const auto& value = object[name];
    if (value.isVoid())
        return fallback;
    return value.isBool() ? std::optional<bool>(static_cast<bool>(value)) : std::nullopt;
}

std::optional<double> readNumber(const juce::var& object, const char* name, double fallback)
{
    const auto& value = object[name];
    if (value.isVoid())
        return fallback;
    if (value.isInt() || value.isInt64() || value.isDouble())
        return static_cast<double>(value);
    return std::nullopt;
}

std::optional<juce::int64> readInteger(const juce::var& object, const char* name,
                                       juce::int64 fallback)
{
    const auto& value = object[name];
    if (value.isVoid())
        return fallback;
    if (value.isInt() || value.isInt64())
        return static_cast<juce::int64>(value);
    return std::nullopt;
}

std::optional<juce::var> readObject(const juce::var& object, const char* name)
{
    const auto& value = object[name];
    if (value.isVoid())
        return juce::var(new juce::DynamicObject());
    return value.isObject() ? std::optional<juce::var>(value) : std::nullopt;
}

// Ids from the file are kept when they are valid and unique; the others become 0 and
// are replaced once everything is loaded.
ObjectId acceptId(const juce::var& object, std::set<ObjectId>& used)
{
    const auto& value = object[json::id];
    if (!value.isInt() && !value.isInt64())
        return 0;

    const auto id = static_cast<ObjectId>(value);
    if (id <= 0 || !used.insert(id).second)
        return 0;

    return id;
}

void assignMissingIds(juce::ValueTree tree)
{
    if ((tree.hasType(ids::track) || tree.hasType(ids::clip)) &&
        static_cast<ObjectId>(tree[ids::id]) == 0)
        tree.setProperty(ids::id, detail::nextObjectId(tree), nullptr);

    for (auto child : tree)
        assignMissingIds(child);
}

} // namespace

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

    // Tempo and metre
    const auto tempo = readNumber(projectJson, json::tempo, TempoMap::defaultTempo);
    if (!tempo)
        return damaged(*version, "the tempo is not a number");
    project.setTempo(*tempo);

    const auto signatureJson = readObject(projectJson, json::timeSignature);
    const auto numerator =
        signatureJson ? readInteger(*signatureJson, json::numerator, 4) : std::nullopt;
    const auto denominator =
        signatureJson ? readInteger(*signatureJson, json::denominator, 4) : std::nullopt;
    const TimeSignature signature{static_cast<int>(numerator.value_or(0)),
                                  static_cast<int>(denominator.value_or(0))};
    if (!numerator || !denominator || *numerator > TimeSignature::maximumNumerator ||
        *denominator > TimeSignature::maximumDenominator || !signature.isValid())
        return damaged(*version, "the time signature is not valid");
    project.setTimeSignature(signature);

    // Loop
    const auto loopJson = readObject(projectJson, json::loop);
    if (!loopJson)
        return damaged(*version, "loop is not an object");
    const auto loopEnabled = readBool(*loopJson, json::enabled, false);
    if (!loopEnabled)
        return damaged(*version, "loop enabled is not true or false");
    const auto loopStart = readInteger(*loopJson, json::start, 0);
    const auto loopEnd = readInteger(*loopJson, json::end, 0);
    if (!loopStart || !loopEnd)
        return damaged(*version, "the loop region is not in whole ticks");
    project.setLoopEnabled(*loopEnabled);
    project.setLoopRange(*loopStart, *loopEnd);

    // Snap
    const auto snapJson = readObject(projectJson, json::snap);
    if (!snapJson)
        return damaged(*version, "snap is not an object");
    const auto snapEnabled = readBool(*snapJson, json::enabled, true);
    if (!snapEnabled)
        return damaged(*version, "snap enabled is not true or false");
    project.setSnapEnabled(*snapEnabled);
    // An unknown grid is not worth refusing the project for.
    const auto grid = gridDivisionFromString((*snapJson)[json::grid].toString());
    project.setGridDivision(grid.value_or(GridDivision::automatic));

    // Master
    const auto masterJson = readObject(projectJson, json::master);
    const auto masterVolume =
        masterJson ? readNumber(*masterJson, json::volume, 1.0) : std::nullopt;
    if (!masterVolume)
        return damaged(*version, "the master volume is not a number");
    project.setMasterVolume(*masterVolume);

    // Tracks and clips
    const auto& tracks = projectJson[json::tracks];
    if (!tracks.isVoid() && !tracks.isArray())
        return damaged(*version, "tracks is not a list");

    std::set<ObjectId> usedIds;
    const auto* trackList = tracks.getArray();

    for (const auto& trackJson : trackList != nullptr ? *trackList : juce::Array<juce::var>{})
    {
        if (!trackJson.isObject())
            return damaged(*version, "a track is not an object");

        const auto& name = trackJson[json::name];
        if (!name.isVoid() && !name.isString())
            return damaged(*version, "a track name is not text");

        const auto volume = readNumber(trackJson, json::volume, 1.0);
        const auto pan = readNumber(trackJson, json::pan, 0.0);
        const auto mute = readBool(trackJson, json::mute, false);
        const auto solo = readBool(trackJson, json::solo, false);
        if (!volume || !pan)
            return damaged(*version, "a track volume or pan is not a number");
        if (!mute || !solo)
            return damaged(*version, "a track mute or solo is not true or false");

        auto track = project.addTrack(name.toString());
        juce::ValueTree(track.getState())
            .setProperty(ids::id, acceptId(trackJson, usedIds), nullptr);
        track.setVolume(*volume);
        track.setPan(*pan);
        track.setMuted(*mute);
        track.setSoloed(*solo);

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

            const auto position = readInteger(clipJson, json::position, 0);
            const auto offset = readInteger(clipJson, json::offset, 0);
            const auto& lengthJson = clipJson[json::length];
            const bool lengthValid =
                lengthJson.isVoid() || lengthJson.isInt() || lengthJson.isInt64();
            if (!position || !offset || !lengthValid)
                return damaged(*version, "a clip position, offset or length is not in whole ticks");

            ClipBounds bounds{*position, *offset, std::nullopt};
            if (!lengthJson.isVoid())
                bounds.length = static_cast<Ticks>(lengthJson);

            auto clip = track.addClip(fileForPath(audioFile.toString(), projectDirectory), bounds);
            juce::ValueTree(clip.getState())
                .setProperty(ids::id, acceptId(clipJson, usedIds), nullptr);
        }
    }

    assignMissingIds(project.getState());

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
