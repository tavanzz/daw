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
    void openFromCommandLine(const juce::String& commandLine)
    {
        for (const auto& argument : juce::StringArray::fromTokens(commandLine, true))
        {
            const juce::File file(argument.unquoted());
            if (juce::File::isAbsolutePath(argument.unquoted()) && file.existsAsFile())
            {
                controller->openFile(file);
                return;
            }
        }
    }

    std::unique_ptr<daw::app::AppController> controller;
};

} // namespace

START_JUCE_APPLICATION(Application)
