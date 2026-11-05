// The plugin scanner: a program of its own, which the app starts once per plugin
// file (controller::PluginScanner), so that a plugin that crashes or hangs while it
// is loaded takes only this process with it. See controller::runScannerProcess().
//
// A JUCE application (with no window): some plugins need the platform's GUI and its
// event loop when they load (licence dialogs, AUv3 plugins answering later).

#include "daw/controller/PluginScanner.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <csignal>
#include <cstdlib>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace
{

// A plugin that crashes ends the scanner at once, with this code: no crash report or
// "quit unexpectedly" dialog for a crash the app expects and handles.
constexpr int crashedCode = 70;

[[noreturn]] void endAfterCrash(int)
{
    std::_Exit(crashedCode);
}

void exitQuietlyOnCrashes()
{
    juce::SystemStats::setApplicationCrashHandler([](void*) { std::_Exit(crashedCode); });
    for (const int signal : {SIGABRT, SIGSEGV, SIGILL, SIGFPE})
        std::signal(signal, endAfterCrash);
#if JUCE_WINDOWS
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

class ScannerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Plugin Scanner"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        exitQuietlyOnCrashes();
        juce::Process::setDockIconVisible(false);
        const bool started = daw::controller::runScannerProcess(getCommandLineParameterArray(), [this](int code) {
            setApplicationReturnValue(code);
            quit();
        });
        if (!started)
        {
            setApplicationReturnValue(64); // EX_USAGE
            quit();
        }
    }

    void shutdown() override {}
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}
};

} // namespace

START_JUCE_APPLICATION(ScannerApplication)
