#include "AppController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace
{

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        controller = std::make_unique<daw::app::AppController>(getApplicationName());
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

            const juce::File file(text);
            if (!opened && juce::File::isAbsolutePath(text) && file.existsAsFile())
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
