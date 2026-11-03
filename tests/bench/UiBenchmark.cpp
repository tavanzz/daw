// What the desktop interface costs in each theme and animation level, and that it
// leaves the audio thread alone.
//
// Build the release preset and run:
//   open "build/release/tests/daw_ui_bench_artefacts/Release/DAW UI Bench.app"
// (or the executable inside it, to see its output). Not part of CTest: it opens a
// window, always on top, for about four minutes, and its timings depend on the
// machine and the display. Results go to standard output, a Markdown table at the end.
//
// The project: 64 stereo tracks playing 8 files in a loop, each with an equaliser,
// a compressor and a send to one of 4 buses (2 with a reverb, 2 with a delay);
// automation on every track's volume, an equaliser gain and the compressor's
// threshold. The window is 1600 x 1000 with the mixer open. The audio runs on a
// real-time thread of its own, as a device's callback runs: a 512-sample block every
// 10.67 ms at 48 kHz.
//
// For every theme and animation level (full, reduced, off), after 3 s to settle, 20 s
// are measured:
// - frames: how often the window painted, and how long each paint took (from the
//   first component's paint to the last's; what the system does with the result is
//   not included);
// - CPU: the message thread's (the interface) and the whole process's, as a
//   percentage of one core;
// - the audio callback's time (median, 99th percentile, maximum) and how many
//   callbacks missed their deadline.

#include "daw/controller/Session.h"
#include "daw/engine/fx/Compressor.h"
#include "daw/engine/fx/Delay.h"
#include "daw/engine/fx/Equaliser.h"
#include "daw/engine/fx/Reverb.h"
#include "daw/ui/desktop/MainComponent.h"
#include "daw/ui/desktop/ThemeManager.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <sys/resource.h>
 #include <time.h>
#endif

namespace
{
using namespace daw;
using Clock = std::chrono::steady_clock;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr int numTracks = 64;
constexpr int numBuses = 4;
constexpr int numFiles = 8;
constexpr double settleSeconds = 3.0;
constexpr double measureSeconds = 20.0;

double secondsOf(Clock::duration duration)
{
    return std::chrono::duration<double>(duration).count();
}

// CPU time of the calling thread, and of the whole process, in seconds.
double threadCpuSeconds()
{
#if JUCE_WINDOWS
    FILETIME created, exited, kernel, user;
    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
    const auto ticks = [](FILETIME time)
    { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) * 1.0e-7;
#else
    timespec time{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_nsec) * 1.0e-9;
#endif
}

double processCpuSeconds()
{
#if JUCE_WINDOWS
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const auto ticks = [](FILETIME time)
    { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) * 1.0e-7;
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const auto seconds = [](timeval time)
    { return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_usec) * 1.0e-6; };
    return seconds(usage.ru_utime) + seconds(usage.ru_stime);
#endif
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    return values[index];
}

juce::File writeFile(const juce::File& folder, int index)
{
    // A few seconds of decaying tones, different per file.
    const auto file = folder.getChildFile("bench-" + juce::String(index) + ".wav");
    const int length = static_cast<int>(sampleRate * 8.0);
    juce::AudioBuffer<float> samples(2, length);
    const double frequency = 55.0 * std::pow(1.5, index);
    for (int sample = 0; sample < length; ++sample)
    {
        const double time = sample / sampleRate;
        const double envelope = std::exp(-6.0 * std::fmod(time, 0.5));
        const double phase = juce::MathConstants<double>::twoPi * frequency * time;
        const auto value = static_cast<float>(0.4 * envelope * std::sin(phase));
        samples.setSample(0, sample, value);
        samples.setSample(1, sample, value);
    }

    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    const auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                           .withSampleRate(sampleRate)
                                                           .withNumChannels(2)
                                                           .withBitsPerSample(24));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer(samples, 0, length);
    return file;
}

// The window's content: times every paint of the interface, from the first paint call
// of a frame to the last.
class TimingHost final : public juce::Component
{
public:
    explicit TimingHost(juce::Component& child)
    {
        addAndMakeVisible(child);
        setOpaque(true);
    }

    void resized() override
    {
        if (auto* child = getChildComponent(0))
            child->setBounds(getLocalBounds());
    }

    void paint(juce::Graphics&) override { frameStart = Clock::now(); }

    void paintOverChildren(juce::Graphics&) override
    {
        paintSeconds.push_back(secondsOf(Clock::now() - frameStart));
    }

    std::vector<double> paintSeconds;

private:
    Clock::time_point frameStart;
};

// The audio device: a block every block period on a real-time thread of its own, as a
// device's callback runs.
class AudioThread final : private juce::Thread
{
public:
    explicit AudioThread(engine::Engine& engineToRun)
        : juce::Thread("Bench audio")
        , engine(engineToRun)
    {
        const double periodMs = 1000.0 * blockSize / sampleRate;
        startRealtimeThread(juce::Thread::RealtimeOptions{}
                                .withPeriodMs(periodMs)
                                .withProcessingTimeMs(periodMs / 2.0));
    }

    ~AudioThread() override { stopThread(2000); }

    void reset()
    {
        const std::scoped_lock lock(mutex);
        callbackSeconds.clear();
        missed = 0;
    }

    struct Results
    {
        double median = 0.0;
        double p99 = 0.0;
        double maximum = 0.0;
        int missed = 0;
    };

    Results take()
    {
        const std::scoped_lock lock(mutex);
        return {percentile(callbackSeconds, 0.5), percentile(callbackSeconds, 0.99),
                callbackSeconds.empty() ? 0.0
                                        : *std::max_element(callbackSeconds.begin(),
                                                            callbackSeconds.end()),
                missed};
    }

private:
    void run() override
    {
        juce::AudioBuffer<float> block(2, blockSize);
        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(blockSize / sampleRate));
        auto next = Clock::now();

        while (!threadShouldExit())
        {
            const auto start = Clock::now();
            engine.process(block);
            const auto taken = secondsOf(Clock::now() - start);

            {
                // Measurements only (the engine itself takes no lock); the message
                // thread holds it for a moment between runs.
                const std::scoped_lock lock(mutex);
                callbackSeconds.push_back(taken);
                if (taken > secondsOf(period))
                    ++missed;
            }

            next += period;
            std::this_thread::sleep_until(next);
        }
    }

    engine::Engine& engine;
    std::mutex mutex;
    std::vector<double> callbackSeconds;
    int missed = 0;
};

class Bench final : private juce::Timer
{
public:
    Bench()
    {
        engine.prepare(sampleRate, blockSize);
        buildProject();

        appearance::AppearancePreferences preferences;
        preferences.animations.followSystemReduceMotion = false;
        themes = std::make_unique<ui::desktop::ThemeManager>(preferences);

        view = std::make_unique<ui::desktop::MainComponent>(session, devices, commands, *themes);
        host = std::make_unique<TimingHost>(*view);
        window = std::make_unique<juce::DocumentWindow>(
            "DAW UI Bench", juce::Colours::black, juce::DocumentWindow::closeButton);
        window->setUsingNativeTitleBar(true);
        window->setContentNonOwned(host.get(), false);
        window->setSize(1600, 1000);
        window->centreWithSize(1600, 1000);
        window->setAlwaysOnTop(true);
        window->setVisible(true);
        themes->getAnimationClock().setHostVisible(true);

        session.setLoopRange(0, model::ticksPerSecond * 8);
        session.setLoopEnabled(true);
        session.play();
        audio = std::make_unique<AudioThread>(engine);

        for (const auto& theme : themes->getLibrary().getThemes())
            for (const auto level : {appearance::AnimationLevel::full,
                                     appearance::AnimationLevel::reduced,
                                     appearance::AnimationLevel::off})
                runs.push_back({theme.id, level});

        startTimerHz(60);
        startRun();
    }

    ~Bench() override
    {
        stopTimer();
        audio.reset();
        window.reset();
        host.reset();
        view.reset();
        themes.reset();
    }

private:
    struct Run
    {
        juce::String themeId;
        appearance::AnimationLevel level;
    };

    void buildProject()
    {
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("daw-ui-bench");
        folder.createDirectory();
        std::vector<juce::File> files;
        for (int index = 0; index < numFiles; ++index)
            files.push_back(writeFile(folder, index));

        std::vector<model::ObjectId> buses;
        for (int index = 0; index < numBuses; ++index)
        {
            buses.push_back(session.addBus("Bus " + juce::String(index + 1)));
            session.addEffect(controller::ChainOwner::bus(buses.back()),
                              index % 2 == 0 ? engine::fx::Reverb::typeId
                                             : engine::fx::Delay::typeId);
        }

        for (int index = 0; index < numTracks; ++index)
        {
            const auto track = session.addTrack("Track " + juce::String(index + 1));
            session.importAudio({files[static_cast<std::size_t>(index % numFiles)]}, 0, track);

            const auto owner = controller::ChainOwner::track(track);
            const auto equaliser = session.addEffect(owner, engine::fx::Equaliser::typeId);
            const auto compressor = session.addEffect(owner, engine::fx::Compressor::typeId);
            session.addSend(track, buses[static_cast<std::size_t>(index % numBuses)]);

            const auto lane = [&](const model::AutomationTarget& target)
            {
                for (int point = 0; point < 8; ++point)
                    session.addAutomationPoint(owner, target,
                                               {model::ticksPerSecond * point,
                                                0.3 + 0.4 * ((point + index) % 3) / 2.0, 0.0});
            };
            lane({});
            lane({model::AutomationTarget::Kind::effectParameter, equaliser, "bell2Gain"});
            lane({model::AutomationTarget::Kind::effectParameter, compressor, "threshold"});
            session.setAutomationMode(owner, model::AutomationMode::read);
        }

        session.waitForLoads(60000);
        session.update();
    }

    void startRun()
    {
        const auto& run = runs[current];
        themes->setTheme(run.themeId);
        auto settings = themes->getPreferences().animations;
        settings.setLevel(run.level);
        themes->setAnimationSettings(settings);

        phaseStart = Clock::now();
        measuring = false;
    }

    void timerCallback() override
    {
        session.update(); // as the app's controller does

        const auto elapsed = secondsOf(Clock::now() - phaseStart);
        if (!measuring && elapsed >= settleSeconds)
        {
            measuring = true;
            phaseStart = Clock::now();
            host->paintSeconds.clear();
            audio->reset();
            uiCpuStart = threadCpuSeconds();
            processCpuStart = processCpuSeconds();
            return;
        }

        if (measuring && elapsed >= measureSeconds)
            finishRun(elapsed);
    }

    void finishRun(double wallSeconds)
    {
        const auto& run = runs[current];
        const auto& paints = host->paintSeconds;
        const auto audioResults = audio->take();

        const auto line = juce::String::formatted(
            "| %-13s | %-7s | %5.1f | %6.2f | %6.2f | %6.2f | %5.1f | %6.1f "
            "| %5.3f | %5.3f | %5.3f | %d |",
            run.themeId.toRawUTF8(), appearance::displayName(run.level).toRawUTF8(),
            static_cast<double>(paints.size()) / wallSeconds, percentile(paints, 0.5) * 1000.0,
            percentile(paints, 0.95) * 1000.0,
            paints.empty() ? 0.0 : *std::max_element(paints.begin(), paints.end()) * 1000.0,
            100.0 * (threadCpuSeconds() - uiCpuStart) / wallSeconds,
            100.0 * (processCpuSeconds() - processCpuStart) / wallSeconds,
            audioResults.median * 1000.0, audioResults.p99 * 1000.0,
            audioResults.maximum * 1000.0, audioResults.missed);
        lines.add(line);
        std::printf("%s\n", line.toRawUTF8());
        std::fflush(stdout);

        if (++current < runs.size())
        {
            startRun();
            return;
        }

        stopTimer();
        report();
        juce::JUCEApplicationBase::quit();
    }

    void report()
    {
        juce::StringArray text;
        text.add("| Theme | Level | Frames/s | Paint median ms | Paint p95 ms | Paint max ms | "
                 "UI CPU % | Process CPU % | Audio median ms | Audio p99 ms | Audio max ms | "
                 "Missed |");
        text.add("|---|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|");
        text.addArray(lines);
        std::printf("\n%s\n", text.joinIntoString("\n").toRawUTF8());
        std::fflush(stdout);
    }

    engine::Engine engine;
    controller::Session session{engine};
    juce::AudioDeviceManager devices; // never opened: the audio comes from AudioThread
    juce::ApplicationCommandManager commands;
    std::unique_ptr<ui::desktop::ThemeManager> themes;
    std::unique_ptr<ui::desktop::MainComponent> view;
    std::unique_ptr<TimingHost> host;
    std::unique_ptr<juce::DocumentWindow> window;
    std::unique_ptr<AudioThread> audio;

    std::vector<Run> runs;
    std::size_t current = 0;
    Clock::time_point phaseStart;
    bool measuring = false;
    double uiCpuStart = 0.0;
    double processCpuStart = 0.0;
    juce::StringArray lines;
};

class BenchApplication final : public juce::JUCEApplication
{
public:
    // The logo shows the application's name: the app's own.
    const juce::String getApplicationName() override { return "DAW"; }
    const juce::String getApplicationVersion() override { return "1"; }

    void initialise(const juce::String&) override { bench = std::make_unique<Bench>(); }
    void shutdown() override { bench.reset(); }

    // The system may ask to quit (a logout, a quit sent from another app): the bench
    // only stops by itself, once every run is done.
    void systemRequestedQuit() override
    {
        std::printf("(ignored a request to quit)\n");
        std::fflush(stdout);
    }

private:
    std::unique_ptr<Bench> bench;
};
} // namespace

START_JUCE_APPLICATION(BenchApplication)
