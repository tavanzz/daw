#include "SmokeTest.h"

#include "daw/model/MidiClip.h"
#include "daw/model/ProjectFile.h"
#include "daw/services/BuildInfo.h"
#include "daw/services/UserSettings.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace daw::app
{

namespace
{

constexpr int ticksToSettle = 3 * 60; // the app's timer runs at 60 Hz

// One second of a 440 Hz tone, 48 kHz stereo, 24-bit.
bool writeTone(const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    juce::AudioBuffer<float> tone(2, 48000);
    for (int sample = 0; sample < tone.getNumSamples(); ++sample)
    {
        const auto value = 0.25f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 *
                                                               sample / 48000.0));
        tone.setSample(0, sample, value);
        tone.setSample(1, sample, value);
    }
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(48000.0)
                                                  .withNumChannels(2)
                                                  .withBitsPerSample(24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples());
}

} // namespace

SmokeTest::SmokeTest(Options testOptions, controller::Session& sessionToTest, controller::PluginLibrary& library,
                     const juce::File& workFolder)
    : options(std::move(testOptions))
    , session(sessionToTest)
    , plugins(library)
    , folder(workFolder)
{
}

bool SmokeTest::tick()
{
    switch (stage)
    {
    case Stage::start:
        start();
        return false;
    case Stage::scanning:
        // Plugins are scanned by the app's scanner program, in processes of their own.
        plugins.update();
        if (plugins.isScanning() && ++scanTicks < (options.project == "scan" ? 3 * 3600 : 120) * 60)
            return false;
        session.pluginsChanged();
        if (options.project == "scan")
        {
            describeScan();
            writeResult();
            stage = Stage::done;
            return true;
        }
        open();
        return false;
    case Stage::waiting:
        // Files load and plugins instantiate at the session's updates.
        if (++waitedTicks < ticksToSettle)
            return false;
        describe();
        writeResult();
        stage = Stage::done;
        return true;
    case Stage::done:
    default:
        return true;
    }
}

void SmokeTest::start()
{
    const auto& build = services::buildInfo();
    result->setProperty("version", build.version);
    result->setProperty("commit", build.commit);
    result->setProperty("platform", build.platform + " " + build.architecture);
    result->setProperty("settingsVersion", services::currentSettingsVersion);

    if (options.project == "scan")
    {
        plugins.scan(true);
        stage = Stage::scanning;
        return;
    }
    if (options.project == "new" && options.plugin.exists())
    {
        // Only this plugin, not every plugin of the computer.
        plugins.retry({"VST3", options.plugin.getFullPathName()});
        stage = Stage::scanning;
        return;
    }
    open();
}

void SmokeTest::open()
{
    // "new" is not a path: a juce::File of it would be a relative one.
    juce::File project;
    if (options.project != "new")
        project = juce::File(options.project);
    else
    {
        makeProject();
        project = folder.getChildFile(juce::String("Smoke") + model::projectFileExtension);
        const auto saved = session.saveProject(project);
        result->setProperty("saved", saved.wasOk());
        // From the disk again, as a user opens it.
        session.newProject();
    }

    const auto opened = session.openProject(project);
    result->setProperty("opened", opened.wasOk());
    result->setProperty("error", opened.getErrorMessage());
    juce::Logger::writeToLog("Smoke test: " + juce::String(opened.wasOk() ? "opened " : "could not open ") +
                             project.getFileName());
    stage = Stage::waiting;
}

void SmokeTest::makeProject()
{
    session.newProject();

    // The plugin the scanner found.
    std::optional<juce::PluginDescription> plugin;
    if (options.plugin.exists())
    {
        for (const auto& info : plugins.getCatalogue().getPlugins())
        {
            // A file plugin (Audio Units have identifiers instead): the bundle or inside it.
            const auto& identifier = info.description.fileOrIdentifier;
            if (!juce::File::isAbsolutePath(identifier))
                continue;
            if (const juce::File file(identifier); file == options.plugin || file.isAChildOf(options.plugin))
                plugin = info.description;
        }
        result->setProperty("pluginScanned", plugin.has_value());
    }

    const auto tone = folder.getChildFile("Audio/Tone.wav");
    result->setProperty("toneWritten", writeTone(tone));
    const auto clips = session.importAudio({tone}, 0);
    if (!clips.empty() && plugin)
        if (const auto& project = session.getProject(); project.getNumTracks() > 0)
            pluginEffect = session.addPlugin(controller::ChainOwner::track(project.getTrack(0).getId()), *plugin);

    const auto midiTrack = session.addMidiTrack("Synth");
    const auto clip = session.addMidiClip(midiTrack, 0, model::ticksPerSecond * 2);
    model::Note note;
    note.start = 0;
    note.length = model::ticksPerSecond;
    note.key = 60;
    session.addNote(clip, note);
}

void SmokeTest::describe()
{
    const auto& project = session.getProject();
    int clips = 0, notes = 0;
    for (int index = 0; index < project.getNumTracks(); ++index)
    {
        const auto track = project.getTrack(index);
        clips += track.getNumClips();
        for (int clipIndex = 0; clipIndex < track.getNumClips(); ++clipIndex)
            if (const auto midi = session.findMidiClip(track.getClip(clipIndex).getId()))
                notes += static_cast<int>(midi->getNotes().size());
    }
    result->setProperty("tracks", project.getNumTracks());
    result->setProperty("clips", clips);
    result->setProperty("notes", notes);
    result->setProperty("unavailablePlugins", session.getUnavailablePlugins().joinIntoString(", "));
    if (pluginEffect != 0)
        result->setProperty("pluginReady", session.getPluginStatus(pluginEffect) == controller::PluginStatus::ready);
}

void SmokeTest::describeScan()
{
    const auto& catalogue = plugins.getCatalogue();
    result->setProperty("plugins", static_cast<int>(catalogue.getPlugins().size()));
    juce::Array<juce::var> notLoaded;
    for (const auto& blocked : catalogue.getBlocked())
        notLoaded.add(blocked.item.format + " " + blocked.item.item + ": " + blocked.detail);
    result->setProperty("notLoaded", notLoaded);
}

void SmokeTest::writeResult()
{
    if (options.result != juce::File())
        options.result.replaceWithText(juce::JSON::toString(juce::var(result.get())), false, false, "\n");
    juce::Logger::writeToLog("Smoke test: " + juce::JSON::toString(juce::var(result.get()), true));
}

} // namespace daw::app
