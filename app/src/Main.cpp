#include "AppController.h"
#include "daw/services/BuildInfo.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <memory>

#if JUCE_WINDOWS
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

namespace
{

// The value of an option "--name=value" on the command line (quotes removed), if given.
juce::String optionValue(const juce::String& commandLine, const juce::String& name)
{
    for (const auto& argument : juce::StringArray::fromTokens(commandLine, true))
    {
        const auto text = argument.unquoted();
        if (text.startsWith(name + "="))
            return text.fromFirstOccurrenceOf("=", false, false).unquoted();
    }
    return {};
}

// Options that make a run of its own (they never hand over to a running copy of the app):
// printing the version, and runs with settings of their own (--settings-dir=<folder>),
// such as the release checks make.
bool isSeparateRun(const juce::String& commandLine)
{
    return commandLine.contains("--version") || optionValue(commandLine, "--settings-dir").isNotEmpty();
}

// "DAW 0.8.0 (3f2a9c1b7e04, Release, macOS arm64)" on the console that started the app.
void printVersion()
{
#if JUCE_WINDOWS
    // A Windows GUI program has no console of its own: borrow the parent's, if any.
    if (AttachConsole(ATTACH_PARENT_PROCESS) != 0)
    {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
    }
#endif
    std::fputs((daw::services::buildInfo().describe() + "\n").toRawUTF8(), stdout);
    std::fflush(stdout);
}

// A path given on the command line, relative to where the app was started.
juce::File fileOption(const juce::String& commandLine, const juce::String& name)
{
    const auto path = optionValue(commandLine, name);
    return path.isEmpty() ? juce::File() : juce::File::getCurrentWorkingDirectory().getChildFile(path);
}

// The release checks' runs (they need --settings-dir):
//   --smoke-test=new|scan|<project> --smoke-result=<json> [--smoke-plugin=<.vst3>]
//   --update-test=<json> [--smoke-result=<json>]: updates, and the new version then runs
//   the smoke test with the same settings.
daw::app::AppController::Options runOptions(const juce::String& commandLine)
{
    daw::app::AppController::Options options;
    options.settingsFolder = fileOption(commandLine, "--settings-dir");
    if (options.settingsFolder == juce::File())
        return options;

    options.updateTestResult = fileOption(commandLine, "--update-test");
    if (const auto project = optionValue(commandLine, "--smoke-test");
        project.isNotEmpty() && options.updateTestResult == juce::File())
    {
        daw::app::SmokeTest::Options smoke;
        smoke.project = project == "new" || project == "scan" ? project
                                                             : fileOption(commandLine, "--smoke-test").getFullPathName();
        smoke.result = fileOption(commandLine, "--smoke-result");
        smoke.plugin = fileOption(commandLine, "--smoke-plugin");
        options.smokeTest = smoke;
    }
    if (options.updateTestResult != juce::File())
        if (const auto result = fileOption(commandLine, "--smoke-result"); result != juce::File())
            options.relaunchArguments = {"--settings-dir=" + options.settingsFolder.getFullPathName(),
                                         "--smoke-test=new", "--smoke-result=" + result.getFullPathName()};
    return options;
}

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return daw::services::buildInfo().version; }

    bool moreThanOneInstanceAllowed() override
    {
        return isSeparateRun(getCommandLineParameters());
    }

    void initialise(const juce::String& commandLine) override
    {
        if (commandLine.contains("--version"))
        {
            printVersion();
            quit();
            return;
        }

        controller = std::make_unique<daw::app::AppController>(getApplicationName(), runOptions(commandLine));
        openFromCommandLine(commandLine);
    }

    void shutdown() override { controller.reset(); }

    void systemRequestedQuit() override
    {
        if (controller != nullptr)
            controller->requestQuit();
        else
            quit();
    }

    // A second launch (e.g. opening a file from the Finder) hands its arguments here.
    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        openFromCommandLine(commandLine);
    }

private:
    // A project or audio file to open; --show=previews,animations,mixer,editors,browser,
    // plugins opens those too, for reviewing the interface (see docs/LISTENING_TESTS.md).
    void openFromCommandLine(const juce::String& commandLine)
    {
        juce::StringArray show;
        bool opened = false;

        for (const auto& argument : juce::StringArray::fromTokens(commandLine, true))
        {
            const auto text = argument.unquoted();
            if (text.startsWith("--show="))
            {
                show.addTokens(text.fromFirstOccurrenceOf("=", false, false), ",", {});
                continue;
            }

            // Other options are not files; only absolute paths make a juce::File.
            if (opened || text.startsWith("--") || !juce::File::isAbsolutePath(text))
                continue;

            if (const juce::File file(text); file.existsAsFile())
            {
                controller->openFile(file);
                opened = true;
            }
        }

        if (!show.isEmpty())
            controller->showForReview(show);
    }

    std::unique_ptr<daw::app::AppController> controller;
};

} // namespace

START_JUCE_APPLICATION(Application)
