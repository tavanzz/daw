#include "daw/model/ProjectFile.h"

#include "daw/model/FormatVersion.h"

#include "Identifiers.h"
#include "ObjectIds.h"

#include <algorithm>
#include <array>
#include <functional>
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
constexpr const char* input = "input";
constexpr const char* channel = "channel";
constexpr const char* stereo = "stereo";
constexpr const char* monitoring = "monitoring";
constexpr const char* armed = "armed";
constexpr const char* metronome = "metronome";
constexpr const char* countIn = "countIn";
constexpr const char* bars = "bars";
constexpr const char* punch = "punch";
constexpr const char* recording = "recording";
constexpr const char* bitsPerSample = "bitsPerSample";
constexpr const char* buses = "buses";
constexpr const char* effects = "effects";
constexpr const char* type = "type";
constexpr const char* bypass = "bypass";
constexpr const char* parameters = "parameters";
constexpr const char* data = "data";
constexpr const char* sends = "sends";
constexpr const char* bus = "bus";
constexpr const char* level = "level";
constexpr const char* preFader = "preFader";
constexpr const char* automation = "automation";
constexpr const char* mode = "mode";
constexpr const char* lanes = "lanes";
constexpr const char* target = "target";
constexpr const char* points = "points";
constexpr const char* value = "value";
constexpr const char* curve = "curve";
constexpr const char* kind = "kind";
constexpr const char* midiInput = "midiInput";
constexpr const char* device = "device";
constexpr const char* midiChannel = "midiChannel";
constexpr const char* instrument = "instrument";
constexpr const char* files = "files";
constexpr const char* notes = "notes";
constexpr const char* key = "key";
constexpr const char* velocity = "velocity";
constexpr const char* controllers = "controllers";
constexpr const char* events = "events";

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

// Track inputs as stored: {"channel": input number counting from 1, or 0 for none;
// "stereo": the pair starting there}.
juce::var inputToJson(TrackInput input)
{
    return makeObject({{json::channel, input.channel + 1}, {json::stereo, input.stereo}});
}

// Version 3 (Phase 3, recording) adds each track's input, monitoring mode and arm
// state, and the project's metronome, count-in, punch and recording format. The
// values given here are those of a new track and a new project.
juce::var migrateVersion2To3(juce::var root)
{
    auto* project = root[json::project].getDynamicObject();
    if (project == nullptr)
        return root;

    project->setProperty(json::metronome, makeObject({{json::enabled, false},
                                                      {json::volume,
                                                       Project::defaultMetronomeVolume}}));
    project->setProperty(json::countIn, makeObject({{json::enabled, false}, {json::bars, 1}}));
    project->setProperty(json::punch, makeObject({{json::enabled, false}}));
    project->setProperty(json::recording,
                         makeObject({{json::bitsPerSample,
                                      Project::defaultRecordingBitsPerSample}}));

    if (auto* tracks = project->getProperty(json::tracks).getArray())
    {
        for (auto& track : *tracks)
        {
            if (auto* trackObject = track.getDynamicObject())
            {
                trackObject->setProperty(json::input, inputToJson(TrackInput{}));
                trackObject->setProperty(json::monitoring, toString(MonitoringMode::automatic));
                trackObject->setProperty(json::armed, false);
            }
        }
    }

    return root;
}

juce::var emptyAutomation()
{
    return makeObject({{json::mode, toString(AutomationMode::read)},
                       {json::lanes, juce::Array<juce::var>{}}});
}

// Version 4 (Phase 4, effects and automation) adds the aux buses, and effects and
// automation on the master and on every track, which also gets its sends. A
// version 3 project has none of them.
juce::var migrateVersion3To4(juce::var root)
{
    auto* project = root[json::project].getDynamicObject();
    if (project == nullptr)
        return root;

    if (auto* master = project->getProperty(json::master).getDynamicObject())
    {
        master->setProperty(json::effects, juce::Array<juce::var>{});
        master->setProperty(json::automation, emptyAutomation());
    }
    project->setProperty(json::buses, juce::Array<juce::var>{});

    if (auto* tracks = project->getProperty(json::tracks).getArray())
    {
        for (auto& track : *tracks)
        {
            if (auto* trackObject = track.getDynamicObject())
            {
                trackObject->setProperty(json::effects, juce::Array<juce::var>{});
                trackObject->setProperty(json::sends, juce::Array<juce::var>{});
                trackObject->setProperty(json::automation, emptyAutomation());
            }
        }
    }

    return root;
}

// Version 5 (Phase 6, MIDI and instruments) gives tracks and clips a kind, "audio"
// or "midi"; everything in a version 4 project is audio.
juce::var migrateVersion4To5(juce::var root)
{
    auto* project = root[json::project].getDynamicObject();
    if (project == nullptr)
        return root;

    if (auto* tracks = project->getProperty(json::tracks).getArray())
    {
        for (auto& track : *tracks)
        {
            auto* trackObject = track.getDynamicObject();
            if (trackObject == nullptr)
                continue;
            trackObject->setProperty(json::kind, "audio");
            if (auto* clips = trackObject->getProperty(json::clips).getArray())
                for (auto& clip : *clips)
                    if (auto* clipObject = clip.getDynamicObject())
                        clipObject->setProperty(json::kind, "audio");
        }
    }

    return root;
}

// The migration chain of this build: entry i upgrades format version i + 1 to i + 2.
// When the format changes, bump currentFormatVersion and append a migration.
constexpr std::array<FormatMigration, currentFormatVersion - 1> migrations{
    migrateVersion1To2, migrateVersion2To3, migrateVersion3To4, migrateVersion4To5};
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

namespace
{

juce::var effectToJson(const Effect& effect, const juce::File& projectDirectory)
{
    auto parameters = makeObject();
    for (const auto& [id, value] : effect.getParameters())
        parameters.getDynamicObject()->setProperty(id, value);

    auto object = makeObject({{json::id, effect.getId()},
                              {json::type, effect.getType()},
                              {json::bypass, effect.isBypassed()},
                              {json::parameters, parameters}});
    if (const auto files = effect.getFiles(); !files.empty())
    {
        auto filesObject = makeObject();
        for (const auto& [key, file] : files)
            filesObject.getDynamicObject()->setProperty(key, pathForFile(file, projectDirectory));
        object.getDynamicObject()->setProperty(json::files, filesObject);
    }
    if (const auto extra = effect.getExtraState(); !extra.isEmpty())
        object.getDynamicObject()->setProperty(
            json::data, juce::Base64::toBase64(extra.getData(), extra.getSize()));
    return object;
}

juce::var effectsToJson(const EffectList& list, const juce::File& projectDirectory)
{
    juce::Array<juce::var> effects;
    for (int i = 0; i < list.getNumEffects(); ++i)
        effects.add(effectToJson(list.getEffect(i), projectDirectory));
    return effects;
}

juce::var clipToJson(const Clip& clip, const juce::File& projectDirectory)
{
    if (!clip.isMidi())
    {
        auto object = makeObject({
            {json::id, clip.getId()},
            {json::kind, "audio"},
            {json::audioFile, pathForFile(clip.getAudioFile(), projectDirectory)},
            {json::position, clip.getPosition()},
            {json::offset, clip.getOffset()},
        });
        if (const auto length = clip.getLength())
            object.getDynamicObject()->setProperty(json::length, *length);
        return object;
    }

    const MidiClip midi(clip.getState());
    juce::Array<juce::var> notes;
    for (const auto& note : midi.getNotes())
        notes.add(makeObject({{json::id, note.id},
                              {json::start, note.start},
                              {json::length, note.length},
                              {json::key, note.key},
                              {json::velocity, note.velocity}}));

    juce::Array<juce::var> controllers;
    for (const auto& type : midi.getControllerTypes())
    {
        juce::Array<juce::var> events;
        for (const auto& event : midi.getControllerEvents(type))
            events.add(makeObject({{json::position, event.position}, {json::value, event.value}}));
        controllers.add(makeObject({{json::type, type.toString()}, {json::events, events}}));
    }

    return makeObject({
        {json::id, clip.getId()},
        {json::kind, "midi"},
        {json::name, clip.getName()},
        {json::position, clip.getPosition()},
        {json::offset, clip.getOffset()},
        {json::length, clip.getLength().value_or(Clip::minimumLength)},
        {json::notes, notes},
        {json::controllers, controllers},
    });
}

juce::var automationToJson(const Automation& automation)
{
    juce::Array<juce::var> lanes;
    for (int i = 0; i < automation.getNumLanes(); ++i)
    {
        const auto lane = automation.getLane(i);
        const auto target = lane.getTarget();
        if (!target)
            continue;

        juce::Array<juce::var> points;
        for (int p = 0; p < lane.getNumPoints(); ++p)
        {
            const auto point = lane.getPoint(p);
            points.add(makeObject({{json::position, point.position},
                                   {json::value, point.value},
                                   {json::curve, point.curve}}));
        }
        lanes.add(makeObject({{json::target, target->toString()}, {json::points, points}}));
    }
    return makeObject({{json::mode, toString(automation.getMode())}, {json::lanes, lanes}});
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
            clips.add(clipToJson(track.getClip(c), projectDirectory));

        juce::Array<juce::var> sends;
        for (int i = 0; i < track.getNumSends(); ++i)
        {
            const auto send = track.getSend(i);
            sends.add(makeObject({{json::id, send.getId()},
                                  {json::bus, send.getBus()},
                                  {json::level, send.getLevel()},
                                  {json::preFader, send.isPreFader()}}));
        }

        auto trackObject = makeObject({
            {json::id, track.getId()},
            {json::kind, toString(track.getKind())},
            {json::name, track.getName()},
            {json::volume, track.getVolume()},
            {json::pan, track.getPan()},
            {json::mute, track.isMuted()},
            {json::solo, track.isSoloed()},
        });
        auto* object = trackObject.getDynamicObject();
        if (track.isMidi())
        {
            const auto midiInput = track.getMidiInput();
            object->setProperty(json::midiInput, makeObject({{json::device, midiInput.device},
                                                             {json::channel, midiInput.channel}}));
            object->setProperty(json::midiChannel, track.getMidiChannel());
            const auto instrument = track.getInstrument();
            object->setProperty(json::instrument, instrument ? effectToJson(*instrument, projectDirectory)
                                                             : juce::var());
        }
        else
        {
            object->setProperty(json::input, inputToJson(track.getInput()));
        }
        object->setProperty(json::monitoring, toString(track.getMonitoring()));
        object->setProperty(json::armed, track.isArmed());
        object->setProperty(json::effects, effectsToJson(track.getEffects(), projectDirectory));
        object->setProperty(json::sends, sends);
        object->setProperty(json::automation, automationToJson(track.getAutomation()));
        object->setProperty(json::clips, clips);
        tracks.add(trackObject);
    }

    juce::Array<juce::var> buses;
    for (int b = 0; b < project.getNumBuses(); ++b)
    {
        const auto bus = project.getBus(b);
        buses.add(makeObject({
            {json::id, bus.getId()},
            {json::name, bus.getName()},
            {json::volume, bus.getVolume()},
            {json::pan, bus.getPan()},
            {json::mute, bus.isMuted()},
            {json::effects, effectsToJson(bus.getEffects(), projectDirectory)},
            {json::automation, automationToJson(bus.getAutomation())},
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
        {json::master, makeObject({{json::volume, project.getMasterVolume()},
                                   {json::effects,
                                    effectsToJson(project.getMasterEffects(), projectDirectory)},
                                   {json::automation,
                                    automationToJson(project.getMasterAutomation())}})},
        {json::metronome, makeObject({{json::enabled, project.isMetronomeEnabled()},
                                      {json::volume, project.getMetronomeVolume()}})},
        {json::countIn, makeObject({{json::enabled, project.isCountInEnabled()},
                                    {json::bars, project.getCountInBars()}})},
        {json::punch, makeObject({{json::enabled, project.isPunchEnabled()}})},
        {json::recording, makeObject({{json::bitsPerSample, project.getRecordingBitsPerSample()}})},
        {json::buses, buses},
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
    const bool hasId = tree.hasType(ids::track) || tree.hasType(ids::clip) ||
                       tree.hasType(ids::bus) || tree.hasType(ids::effect) ||
                       tree.hasType(ids::send);
    if (hasId && static_cast<ObjectId>(tree[ids::id]) == 0)
        tree.setProperty(ids::id, detail::nextObjectId(tree), nullptr);

    for (auto child : tree)
        assignMissingIds(child);
}

bool isNumber(const juce::var& value)
{
    return value.isInt() || value.isInt64() || value.isDouble();
}

// What an effect's JSON says, checked.
struct EffectFields
{
    juce::String type;
    bool bypass = false;
    std::vector<std::pair<juce::String, double>> values;
    juce::MemoryBlock extra;
    std::vector<std::pair<juce::String, juce::File>> files;
};

juce::String readEffectFields(const juce::var& effectJson, const juce::File& projectDirectory,
                              EffectFields& fields)
{
    if (!effectJson.isObject())
        return "an effect is not an object";
    const auto& type = effectJson[json::type];
    if (!type.isString() || type.toString().isEmpty())
        return "an effect has no type";
    fields.type = type.toString();
    const auto bypass = readBool(effectJson, json::bypass, false);
    if (!bypass)
        return "an effect bypass is not true or false";
    fields.bypass = *bypass;

    const auto& parameters = effectJson[json::parameters];
    if (!parameters.isVoid() && !parameters.isObject())
        return "the parameters of an effect are not an object";
    if (const auto* object = parameters.getDynamicObject())
    {
        for (const auto& property : object->getProperties())
        {
            if (!isNumber(property.value))
                return "an effect parameter is not a number";
            fields.values.emplace_back(property.name.toString(), static_cast<double>(property.value));
        }
    }

    const auto& files = effectJson[json::files];
    if (!files.isVoid() && !files.isObject())
        return "the files of an effect are not an object";
    if (const auto* object = files.getDynamicObject())
    {
        for (const auto& property : object->getProperties())
        {
            const auto key = property.name.toString();
            if (!property.value.isString() ||
                !key.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"))
                return "a file of an effect is not valid";
            fields.files.emplace_back(key, fileForPath(property.value.toString(), projectDirectory));
        }
    }

    const auto& data = effectJson[json::data];
    juce::MemoryOutputStream extra;
    if (!data.isVoid() &&
        (!data.isString() || !juce::Base64::convertFromBase64(extra, data.toString())))
        return "the data of an effect is not valid";
    fields.extra = extra.getMemoryBlock();
    return {};
}

void applyEffectFields(Effect effect, const EffectFields& fields, const juce::var& effectJson,
                       std::set<ObjectId>& usedIds)
{
    effect.setBypassed(fields.bypass);
    effect.setExtraState(fields.extra);
    for (const auto& [key, file] : fields.files)
        effect.setFile(key, file);
    juce::ValueTree(effect.getState()).setProperty(ids::id, acceptId(effectJson, usedIds), nullptr);
}

// Reads an owner's effects into list: a message for the user if they are damaged.
juce::String readEffects(const juce::var& owner, EffectList list, const juce::File& projectDirectory,
                         std::set<ObjectId>& usedIds)
{
    const auto& effects = owner[json::effects];
    if (effects.isVoid())
        return {};
    if (!effects.isArray())
        return "the effects are not a list";

    for (const auto& effectJson : *effects.getArray())
    {
        EffectFields fields;
        if (const auto error = readEffectFields(effectJson, projectDirectory, fields);
            error.isNotEmpty())
            return error;
        applyEffectFields(list.add(fields.type, -1, fields.values), fields, effectJson, usedIds);
    }
    return {};
}

// Reads a MIDI clip's notes and controllers into it.
juce::String readMidiContent(const juce::var& clipJson, MidiClip clip)
{
    const auto& notesJson = clipJson[json::notes];
    if (!notesJson.isVoid() && !notesJson.isArray())
        return "the notes of a clip are not a list";

    std::vector<Note> notes;
    for (const auto& noteJson : notesJson.isArray() ? *notesJson.getArray() : juce::Array<juce::var>{})
    {
        const auto id = readInteger(noteJson, json::id, 0);
        const auto start = readInteger(noteJson, json::start, 0);
        const auto length = readInteger(noteJson, json::length, 1);
        const auto key = readInteger(noteJson, json::key, 60);
        const auto velocity = readInteger(noteJson, json::velocity, Note::defaultVelocity);
        if (!noteJson.isObject() || !id || !start || !length || !key || !velocity || *start < 0 ||
            *length < 1 || *key < 0 || *key > 127 || *velocity < Note::minimumVelocity ||
            *velocity > Note::maximumVelocity || *id < 0 || *id > std::numeric_limits<std::int32_t>::max())
            return "a note is not valid";
        notes.push_back({static_cast<std::int32_t>(*id), *start, *length, static_cast<int>(*key),
                         static_cast<int>(*velocity)});
    }

    const auto& controllersJson = clipJson[json::controllers];
    if (!controllersJson.isVoid() && !controllersJson.isArray())
        return "the controllers of a clip are not a list";

    std::vector<ControllerEvent> events;
    for (const auto& laneJson :
         controllersJson.isArray() ? *controllersJson.getArray() : juce::Array<juce::var>{})
    {
        if (!laneJson.isObject())
            return "a controller lane is not an object";
        const auto type = ControllerType::fromString(laneJson[json::type].toString());
        const auto& eventsJson = laneJson[json::events];
        if (!eventsJson.isVoid() && !eventsJson.isArray())
            return "the events of a controller lane are not a list";
        if (!type)
            continue; // a newer kind of controller

        for (const auto& eventJson : eventsJson.isArray() ? *eventsJson.getArray() : juce::Array<juce::var>{})
        {
            const auto position = readInteger(eventJson, json::position, 0);
            const auto value = readInteger(eventJson, json::value, 0);
            if (!eventJson.isObject() || !position || !value || *position < 0 || *value < 0 ||
                *value > type->maximumValue())
                return "a controller event is not valid";
            events.push_back({*type, *position, static_cast<int>(*value)});
        }
    }

    clip.setNotes(std::move(notes));
    clip.setControllerEvents(std::move(events));
    return {};
}

// Reads an owner's automation; lanes of targets this version does not know are left
// out (a newer kind of target, or a damaged one).
juce::String readAutomation(const juce::var& owner, Automation automation)
{
    const auto& automationJson = owner[json::automation];
    if (automationJson.isVoid())
        return {};
    if (!automationJson.isObject())
        return "automation is not an object";

    automation.setMode(automationModeFromString(automationJson[json::mode].toString())
                           .value_or(AutomationMode::read));

    const auto& lanes = automationJson[json::lanes];
    if (lanes.isVoid())
        return {};
    if (!lanes.isArray())
        return "the automation lanes are not a list";

    for (const auto& laneJson : *lanes.getArray())
    {
        if (!laneJson.isObject())
            return "an automation lane is not an object";
        const auto& points = laneJson[json::points];
        if (!points.isVoid() && !points.isArray())
            return "the points of an automation lane are not a list";

        const auto target = AutomationTarget::fromString(laneJson[json::target].toString());
        if (!target)
            continue;

        auto lane = automation.getOrAddLane(*target);
        for (const auto& pointJson : points.isArray() ? *points.getArray() : juce::Array<juce::var>{})
        {
            const auto position = readInteger(pointJson, json::position, 0);
            const auto value = readNumber(pointJson, json::value, 0.0);
            const auto curve = readNumber(pointJson, json::curve, 0.0);
            if (!pointJson.isObject() || !position || !value || !curve)
                return "an automation point is not valid";
            lane.addPoint({*position, *value, *curve});
        }
    }
    return {};
}

// Sends to buses that are not there, and lanes of sends or effects that are not
// there (or of targets the owner cannot have), are dropped.
void removeDanglingReferences(Project& project)
{
    const auto pruneLanes = [](Automation automation, const EffectList& effects,
                               const std::function<bool(ObjectId)>& hasSend, bool hasPan,
                               ObjectId instrumentId) {
        for (int i = automation.getNumLanes() - 1; i >= 0; --i)
        {
            const auto target = automation.getLane(i).getTarget();
            bool valid = target.has_value();
            if (valid && target->kind == AutomationTarget::Kind::pan)
                valid = hasPan;
            if (valid && target->kind == AutomationTarget::Kind::sendLevel)
                valid = hasSend(target->object);
            if (valid && target->kind == AutomationTarget::Kind::effectParameter)
                valid = effects.indexOf(target->object) >= 0 ||
                        (instrumentId != 0 && target->object == instrumentId);
            if (!valid)
                automation.removeLane(i);
        }
    };

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack(t);
        for (int i = track.getNumSends() - 1; i >= 0; --i)
            if (project.indexOfBus(track.getSend(i).getBus()) < 0)
                track.removeSend(i);
        const auto instrument = track.getInstrument();
        pruneLanes(track.getAutomation(), track.getEffects(),
                   [&track](ObjectId id) { return track.indexOfSend(id) >= 0; }, true,
                   instrument ? instrument->getId() : 0);
    }

    for (int b = 0; b < project.getNumBuses(); ++b)
    {
        const auto bus = project.getBus(b);
        pruneLanes(bus.getAutomation(), bus.getEffects(), [](ObjectId) { return false; }, true, 0);
    }

    pruneLanes(project.getMasterAutomation(), project.getMasterEffects(),
               [](ObjectId) { return false; }, false, 0);
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

    std::set<ObjectId> usedIds;
    if (const auto error =
            readEffects(*masterJson, project.getMasterEffects(), projectDirectory, usedIds);
        error.isNotEmpty())
        return damaged(*version, "master: " + error);
    if (const auto error = readAutomation(*masterJson, project.getMasterAutomation());
        error.isNotEmpty())
        return damaged(*version, "master: " + error);

    // Recording
    const auto metronomeJson = readObject(projectJson, json::metronome);
    const auto metronomeEnabled =
        metronomeJson ? readBool(*metronomeJson, json::enabled, false) : std::nullopt;
    const auto metronomeVolume =
        metronomeJson ? readNumber(*metronomeJson, json::volume, Project::defaultMetronomeVolume)
                      : std::nullopt;
    if (!metronomeEnabled || !metronomeVolume)
        return damaged(*version, "the metronome settings are not valid");
    project.setMetronomeEnabled(*metronomeEnabled);
    project.setMetronomeVolume(*metronomeVolume);

    const auto countInJson = readObject(projectJson, json::countIn);
    const auto countInEnabled =
        countInJson ? readBool(*countInJson, json::enabled, false) : std::nullopt;
    const auto countInBars = countInJson ? readInteger(*countInJson, json::bars, 1) : std::nullopt;
    if (!countInEnabled || !countInBars)
        return damaged(*version, "the count-in settings are not valid");
    project.setCountInEnabled(*countInEnabled);
    project.setCountInBars(static_cast<int>(
        std::clamp<juce::int64>(*countInBars, 1, Project::maximumCountInBars)));

    const auto punchJson = readObject(projectJson, json::punch);
    const auto punchEnabled = punchJson ? readBool(*punchJson, json::enabled, false) : std::nullopt;
    if (!punchEnabled)
        return damaged(*version, "punch enabled is not true or false");
    project.setPunchEnabled(*punchEnabled);

    // An unsupported format is not worth refusing the project for: it records in the
    // default one.
    const auto recordingJson = readObject(projectJson, json::recording);
    const auto bitsPerSample =
        recordingJson ? readInteger(*recordingJson, json::bitsPerSample,
                                    Project::defaultRecordingBitsPerSample)
                      : std::nullopt;
    if (!bitsPerSample)
        return damaged(*version, "the recording format is not valid");
    project.setRecordingBitsPerSample(
        static_cast<int>(std::clamp<juce::int64>(*bitsPerSample, 0, 64)));

    // Aux buses
    const auto& buses = projectJson[json::buses];
    if (!buses.isVoid() && !buses.isArray())
        return damaged(*version, "buses is not a list");

    for (const auto& busJson : buses.isArray() ? *buses.getArray() : juce::Array<juce::var>{})
    {
        if (!busJson.isObject())
            return damaged(*version, "a bus is not an object");
        const auto& name = busJson[json::name];
        const auto volume = readNumber(busJson, json::volume, 1.0);
        const auto pan = readNumber(busJson, json::pan, 0.0);
        const auto mute = readBool(busJson, json::mute, false);
        if ((!name.isVoid() && !name.isString()) || !volume || !pan || !mute)
            return damaged(*version, "a bus name, volume, pan or mute is not valid");

        auto bus = project.addBus(name.toString());
        juce::ValueTree(bus.getState()).setProperty(ids::id, acceptId(busJson, usedIds), nullptr);
        bus.setVolume(*volume);
        bus.setPan(*pan);
        bus.setMuted(*mute);
        if (const auto error = readEffects(busJson, bus.getEffects(), projectDirectory, usedIds);
            error.isNotEmpty())
            return damaged(*version, "a bus: " + error);
        if (const auto error = readAutomation(busJson, bus.getAutomation()); error.isNotEmpty())
            return damaged(*version, "a bus: " + error);
    }

    // Tracks and clips
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

        const auto volume = readNumber(trackJson, json::volume, 1.0);
        const auto pan = readNumber(trackJson, json::pan, 0.0);
        const auto mute = readBool(trackJson, json::mute, false);
        const auto solo = readBool(trackJson, json::solo, false);
        if (!volume || !pan)
            return damaged(*version, "a track volume or pan is not a number");
        if (!mute || !solo)
            return damaged(*version, "a track mute or solo is not true or false");

        const auto& kindJson = trackJson[json::kind];
        const auto kind = trackKindFromString(kindJson.isVoid() ? "audio" : kindJson.toString());
        if (!kind)
            return damaged(*version, "a track is neither audio nor MIDI");

        const auto inputJson = readObject(trackJson, json::input);
        const auto inputChannel =
            inputJson ? readInteger(*inputJson, json::channel, 1) : std::nullopt;
        const auto inputStereo =
            inputJson ? readBool(*inputJson, json::stereo, false) : std::nullopt;
        if (!inputChannel || !inputStereo || *inputChannel < 0 ||
            *inputChannel > TrackInput::maximumChannels)
            return damaged(*version, "a track input is not valid");

        const auto armed = readBool(trackJson, json::armed, false);
        if (!armed)
            return damaged(*version, "a track arm state is not true or false");
        // An unknown monitoring mode is not worth refusing the project for.
        const auto monitoring = monitoringModeFromString(trackJson[json::monitoring].toString())
                                    .value_or(MonitoringMode::automatic);

        auto track = project.addTrack(name.toString(), *kind);
        juce::ValueTree(track.getState())
            .setProperty(ids::id, acceptId(trackJson, usedIds), nullptr);
        track.setVolume(*volume);
        track.setPan(*pan);
        track.setMuted(*mute);
        track.setSoloed(*solo);
        if (*kind == TrackKind::audio)
            track.setInput({static_cast<int>(*inputChannel) - 1, *inputStereo});
        track.setMonitoring(monitoring);
        track.setArmed(*armed);

        if (const auto error = readEffects(trackJson, track.getEffects(), projectDirectory, usedIds);
            error.isNotEmpty())
            return damaged(*version, "a track: " + error);

        if (track.isMidi())
        {
            const auto midiInputJson = readObject(trackJson, json::midiInput);
            const auto& device = midiInputJson ? (*midiInputJson)[json::device] : juce::var();
            const auto midiInputChannel =
                midiInputJson ? readInteger(*midiInputJson, json::channel, 0) : std::nullopt;
            const auto midiChannel = readInteger(trackJson, json::midiChannel, 1);
            if (!midiInputJson || (!device.isVoid() && !device.isString()) || !midiInputChannel ||
                *midiInputChannel < 0 || *midiInputChannel > 16 || !midiChannel ||
                *midiChannel < 1 || *midiChannel > 16)
                return damaged(*version, "a track's MIDI input or channel is not valid");
            track.setMidiInput({device.toString(), static_cast<int>(*midiInputChannel)});
            track.setMidiChannel(static_cast<int>(*midiChannel));

            const auto& instrumentJson = trackJson[json::instrument];
            if (!instrumentJson.isVoid())
            {
                EffectFields fields;
                if (const auto error = readEffectFields(instrumentJson, projectDirectory, fields);
                    error.isNotEmpty())
                    return damaged(*version, "a track's instrument: " + error);
                applyEffectFields(track.setInstrument(fields.type, fields.values), fields,
                                  instrumentJson, usedIds);
            }
        }

        const auto& sends = trackJson[json::sends];
        if (!sends.isVoid() && !sends.isArray())
            return damaged(*version, "the sends of a track are not a list");
        for (const auto& sendJson : sends.isArray() ? *sends.getArray() : juce::Array<juce::var>{})
        {
            const auto busId = readInteger(sendJson, json::bus, 0);
            const auto level = readNumber(sendJson, json::level, 1.0);
            const auto preFader = readBool(sendJson, json::preFader, false);
            if (!sendJson.isObject() || !busId || !level || !preFader)
                return damaged(*version, "a send is not valid");
            if (track.indexOfSendTo(*busId) >= 0)
                continue; // one send per bus

            auto send = track.addSend(*busId);
            juce::ValueTree(send.getState()).setProperty(ids::id, acceptId(sendJson, usedIds), nullptr);
            send.setLevel(*level);
            send.setPreFader(*preFader);
        }

        if (const auto error = readAutomation(trackJson, track.getAutomation()); error.isNotEmpty())
            return damaged(*version, "a track: " + error);

        const auto& clips = trackJson[json::clips];
        if (!clips.isVoid() && !clips.isArray())
            return damaged(*version, "the clips of a track are not a list");

        const auto* clipList = clips.getArray();
        if (clipList == nullptr)
            continue;

        for (const auto& clipJson : *clipList)
        {
            if (!clipJson.isObject())
                return damaged(*version, "a clip is not an object");
            const bool midiClip = clipJson[json::kind].toString() == "midi";
            if (midiClip != (*kind == TrackKind::midi))
                return damaged(*version, "a clip is not of its track's kind");

            if (midiClip)
            {
                const auto& clipName = clipJson[json::name];
                const auto position = readInteger(clipJson, json::position, 0);
                const auto offset = readInteger(clipJson, json::offset, 0);
                const auto length = readInteger(clipJson, json::length, 0);
                if ((!clipName.isVoid() && !clipName.isString()) || !position || !offset ||
                    !length || *length < Clip::minimumLength)
                    return damaged(*version, "a MIDI clip's name, position, offset or length is not valid");
                auto clip = track.addMidiClip(clipName.toString(), {*position, *offset, *length});
                if (const auto error = readMidiContent(clipJson, MidiClip(clip.getState()));
                    error.isNotEmpty())
                    return damaged(*version, "a MIDI clip: " + error);
                juce::ValueTree(clip.getState())
                    .setProperty(ids::id, acceptId(clipJson, usedIds), nullptr);
                continue;
            }

            const auto& audioFile = clipJson[json::audioFile];
            if (!audioFile.isString())
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
    removeDanglingReferences(project);

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
