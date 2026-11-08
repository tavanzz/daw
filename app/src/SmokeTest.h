#pragma once

#include "daw/controller/PluginLibrary.h"
#include "daw/controller/Session.h"

#include <juce_core/juce_core.h>

namespace daw::app
{

/**
    What the release checks run in the installed app (--smoke-test): it makes a project
    (an audio track with a tone, a MIDI track with the synth playing a note, and a plugin
    on the audio track when --smoke-plugin names one, which the app's own scanner must
    find), saves it, opens it again from the disk and checks it; or it opens a given
    project (a newer one must be refused). It writes what it found as JSON (--smoke-result)
    and the app quits normally.

    Driven by the app's timer: tick() until it returns true. Message thread only.
*/
class SmokeTest
{
public:
    struct Options
    {
        juce::String project; ///< "new", or a project file to open
        juce::File result;    ///< where the JSON goes
        juce::File plugin;    ///< a VST3 plugin to scan and use (optional)
    };

    SmokeTest(Options options, controller::Session& session, controller::PluginLibrary& plugins,
              const juce::File& workFolder);

    /** True once the result is written. */
    bool tick();

private:
    enum class Stage
    {
        start,
        scanning,
        waiting,
        done
    };

    void start();
    void open();
    void makeProject();
    void describe();
    void writeResult();

    Options options;
    controller::Session& session;
    controller::PluginLibrary& plugins;
    juce::File folder;
    Stage stage = Stage::start;
    int waitedTicks = 0;
    int scanTicks = 0;
    model::ObjectId pluginEffect = 0;
    juce::DynamicObject::Ptr result{new juce::DynamicObject()};
};

} // namespace daw::app
