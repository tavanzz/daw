#include "daw/ui/desktop/MainWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace
{

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        mainWindow = std::make_unique<daw::ui::desktop::MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<daw::ui::desktop::MainWindow> mainWindow;
};

} // namespace

START_JUCE_APPLICATION(Application)
