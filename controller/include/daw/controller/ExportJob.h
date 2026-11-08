#pragma once

#include "daw/engine/fx/EffectInstance.h"
#include "daw/engine/io/Bounce.h"
#include "daw/model/Clip.h"

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <map>
#include <memory>

namespace daw::controller
{

class Session;

/** What the user chose in the export dialog. */
struct ExportSettings
{
    enum class Range
    {
        wholeProject, ///< from 0 to the end of the last clip
        loopRegion    ///< the loop region (the whole project if it is empty)
    };

    juce::File file;
    double sampleRate = 48000.0;
    int bitsPerSample = 24; ///< 16, 24 or 32 (float)
    bool dither = false;
    Range range = Range::wholeProject;
    /** The effects' tails after the end (see engine::io::BounceSettings). */
    bool includeTail = true;
};

/** The sample rates and bit depths the export dialog offers. */
inline constexpr double exportSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0};
inline constexpr int exportBitDepths[] = {16, 24, 32};

/**
    Exports a project to a WAV file away from the message thread.

    The constructor (message thread) takes a copy of the project and fresh instances
    of its plugins (Session::makeExportPlugins(): the state each plugin holds now,
    offline); run() (any one thread) opens its files at the export's sample rate,
    streaming them as it goes (it reads from disk as needed, from the original files),
    builds the arrangement and writes it with the same mixer as playback.
*/
class ExportJob
{
public:
    ExportJob(const Session& session, ExportSettings settingsToUse);

    /** keepGoing gets the progress (0 to 1) and returns false to cancel. */
    [[nodiscard]] engine::io::BounceResult run(const std::function<bool(double)>& keepGoing = {});

    [[nodiscard]] const ExportSettings& getSettings() const noexcept { return settings; }

    /** Plugins left out of the export: missing here, or they did not load. */
    [[nodiscard]] const juce::StringArray& getMissingPlugins() const noexcept { return missingPlugins; }

private:
    juce::ValueTree projectState; // a copy: only run()'s thread touches it
    ExportSettings settings;
    // Plugins of their own, loaded and given their state by the constructor.
    std::map<model::ObjectId, std::shared_ptr<engine::fx::EffectInstance>> plugins;
    juce::StringArray missingPlugins;
};

} // namespace daw::controller
