#include "AppController.h"

#include "daw/engine/io/AudioFileLoader.h"
#include "daw/model/ProjectFile.h"
#include "daw/ui/desktop/TransportBar.h"

namespace daw::app
{

namespace
{
// Used to load files while no audio device is running; they are reloaded at the
// device's rate once one starts.
constexpr double fallbackSampleRate = 48000.0;
constexpr const char* deviceStateKey = "audioDevice";

juce::String describe(const engine::io::LoadedAudioFile& loaded, double playbackRate)
{
    juce::StringArray parts;
    parts.add(loaded.formatName.upToFirstOccurrenceOf(" ", false, false));
    parts.add(juce::String(loaded.sourceSampleRate / 1000.0, 1) + " kHz");
    parts.add(juce::String(loaded.sourceBitsPerSample) + "-bit");
    parts.add(loaded.numChannels == 1 ? "mono" : (loaded.numChannels == 2 ? "stereo"
                                                                          : juce::String(loaded.numChannels) + " channels"));
    parts.add(ui::desktop::formatTime(static_cast<double>(loaded.audio->samples.getNumSamples()) /
                                      playbackRate));

    if (!juce::exactlyEqual(loaded.sourceSampleRate, playbackRate))
        parts.add("played at " + juce::String(playbackRate / 1000.0, 1) + " kHz");

    return parts.joinIntoString("  ·  ");
}
} // namespace

struct AppController::LoadOutcome
{
    engine::io::LoadedAudioFile loaded;
    std::shared_ptr<const engine::dsp::WaveformPeaks> peaks;
};

AppController::AppController(const juce::String& name)
    : applicationName(name)
{
    juce::PropertiesFile::Options options;
    options.applicationName = applicationName;
    options.filenameSuffix = ".settings";
    options.folderName = applicationName;
    options.osxLibrarySubFolder = "Application Support";
    settings.setStorageParameters(options);

    const auto savedDevice = settings.getUserSettings()->getXmlValue(deviceStateKey);
    const auto deviceError = deviceHost.initialise(savedDevice.get());
    deviceHost.getDeviceManager().addChangeListener(this);

    window = std::make_unique<ui::desktop::MainWindow>(applicationName, engine,
                                                       deviceHost.getDeviceManager());
    auto& mainComponent = view();
    mainComponent.onOpenAudioFile = [this](const juce::File& file) { openAudioFile(file); };
    mainComponent.onOpenProject = [this](const juce::File& file) { openProject(file); };
    mainComponent.onSaveProject = [this] { return saveProject(); };
    mainComponent.onSaveProjectAs = [this](const juce::File& file) { saveProjectAs(file); };
    mainComponent.onLoopToggled = [this](bool shouldLoop) { setLooping(shouldLoop); };

    if (deviceError.isNotEmpty())
        mainComponent.showError("No audio output", "The audio output could not be opened: " +
                                                       deviceError +
                                                       "\n\nChoose another one in File > Audio Output.");

    updateWindowTitle();
    startTimerHz(2); // frees audio the engine has finished with
}

AppController::~AppController()
{
    stopTimer();
    saveDeviceState();
    deviceHost.getDeviceManager().removeChangeListener(this);
    window.reset();
    loader.removeAllJobs(true, 10000);
}

void AppController::openFile(const juce::File& file)
{
    if (file.hasFileExtension(juce::String(model::projectFileExtension).substring(1)))
        openProject(file);
    else
        openAudioFile(file);
}

void AppController::openAudioFile(const juce::File& file)
{
    const int generation = ++loadGeneration;
    const double sampleRate = loadingSampleRate();
    view().showMessage("Loading \"" + file.getFileName() + "\"...");

    juce::WeakReference<AppController> weakThis(this);
    loader.addJob([weakThis, generation, file, sampleRate] {
        auto outcome = std::make_shared<LoadOutcome>();
        outcome->loaded = engine::io::loadAudioFile(file, sampleRate);
        if (outcome->loaded.succeeded())
            outcome->peaks = std::make_shared<const engine::dsp::WaveformPeaks>(
                engine::dsp::WaveformPeaks::compute(outcome->loaded.audio->samples));

        juce::MessageManager::callAsync([weakThis, generation, file, outcome] {
            if (auto* self = weakThis.get())
                self->finishLoading(generation, file, *outcome);
        });
    });
}

void AppController::finishLoading(int generation, const juce::File& file, LoadOutcome& outcome)
{
    if (generation != loadGeneration)
        return; // a newer file was asked for meanwhile

    if (!outcome.loaded.succeeded())
    {
        restoreDisplay();
        view().showError("Could not open the audio file", outcome.loaded.error);
        return;
    }

    audioFile = file;
    audioSampleRate = outcome.loaded.audio->sampleRate;
    audioDetails = describe(outcome.loaded, audioSampleRate);
    audioPeaks = outcome.peaks;
    playWholeFile(std::move(*outcome.loaded.audio));
    restoreDisplay();

    // Phase 1 projects hold one track with one clip: the file being played.
    if (project.getNumTracks() == 0)
        project.addTrack("Track 1");
    auto track = project.getTrack(0);
    while (track.getNumClips() > 0)
        track.removeClip(0);
    track.addClip(file);

    updateWindowTitle();
}

void AppController::playWholeFile(engine::AudioData audio)
{
    // Until the multitrack session takes over: one track playing the whole file.
    auto source = std::make_shared<const engine::MemoryAudioSource>(std::move(audio));
    auto arrangement = std::make_unique<engine::Arrangement>();
    arrangement->sampleRate = source->getSampleRate();
    arrangement->length = source->getLength();

    engine::ArrangementTrack track;
    track.id = 1;
    track.segments.push_back({source.get(), 0, source->getLength(), 0, 0, 0});
    arrangement->tracks.push_back(std::move(track));
    arrangement->sources.push_back(std::move(source));

    engine.resetTransport();
    engine.setArrangement(std::move(arrangement));
}

void AppController::restoreDisplay()
{
    if (audioPeaks != nullptr)
        view().showAudio(audioFile.getFileName(), audioDetails, audioPeaks);
    else
        view().showMessage(ui::desktop::MainComponent::emptyMessage());
}

void AppController::openProject(const juce::File& file)
{
    auto loaded = model::loadProject(file);
    if (!loaded.succeeded())
    {
        view().showError("Could not open the project", loaded.error);
        return;
    }

    project = std::move(*loaded.project);
    projectFile = file;
    engine.setLooping(project.isLoopEnabled());

    const bool hasClip = project.getNumTracks() > 0 && project.getTrack(0).getNumClips() > 0;
    const auto clipFile = hasClip ? project.getTrack(0).getClip(0).getAudioFile() : juce::File();

    if (clipFile.existsAsFile())
    {
        openAudioFile(clipFile);
    }
    else
    {
        ++loadGeneration; // forget any load still running for the previous project
        audioFile = juce::File();
        audioPeaks.reset();
        engine.resetTransport();
        auto empty = std::make_unique<engine::Arrangement>(); // nothing to play
        empty->sampleRate = engine.getSampleRate();
        engine.setArrangement(std::move(empty));
        restoreDisplay();

        if (hasClip)
            view().showError("Audio file not found",
                             "The project's audio file is missing:\n" + clipFile.getFullPathName());
    }

    updateWindowTitle();
}

bool AppController::saveProject()
{
    if (projectFile == juce::File())
        return false;

    saveProjectAs(projectFile);
    return true;
}

void AppController::saveProjectAs(const juce::File& file)
{
    const auto target = file.withFileExtension(model::projectFileExtension);
    const auto result = model::saveProject(project, target);

    if (result.failed())
    {
        view().showError("Could not save the project", result.getErrorMessage());
        return;
    }

    projectFile = target;
    updateWindowTitle();
}

void AppController::setLooping(bool shouldLoop)
{
    engine.setLooping(shouldLoop);
    project.setLoopEnabled(shouldLoop);
}

void AppController::updateWindowTitle()
{
    juce::String title = applicationName;
    if (projectFile != juce::File())
        title << "  -  " << projectFile.getFileNameWithoutExtension();
    else if (audioFile != juce::File())
        title << "  -  " << audioFile.getFileName();

    window->setName(title);
}

void AppController::saveDeviceState()
{
    if (const auto state = deviceHost.getDeviceManager().createStateXml())
    {
        settings.getUserSettings()->setValue(deviceStateKey, state.get());
        settings.saveIfNeeded();
    }
}

double AppController::loadingSampleRate() const
{
    const double deviceRate = engine.getSampleRate();
    return deviceRate > 0.0 ? deviceRate : fallbackSampleRate;
}

ui::desktop::MainComponent& AppController::view()
{
    return window->getMainComponent();
}

void AppController::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // The device changed (chosen by the user, or unplugged). Remember the choice, and
    // convert the audio again if the sample rate is no longer the one it was made for.
    saveDeviceState();

    const double deviceRate = engine.getSampleRate();
    if (audioFile.existsAsFile() && deviceRate > 0.0 && !juce::exactlyEqual(deviceRate, audioSampleRate))
        openAudioFile(audioFile);
}

void AppController::timerCallback()
{
    engine.collectGarbage();
}

} // namespace daw::app
