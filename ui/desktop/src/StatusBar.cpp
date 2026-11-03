#include "daw/ui/desktop/StatusBar.h"

namespace daw::ui::desktop
{

namespace
{
using appearance::ColourRole;

juce::String diskTimeLeft(double seconds)
{
    if (seconds < 0.0)
        return {};

    const auto minutes = static_cast<int>(seconds / 60.0);
    if (minutes >= 120)
        return juce::String(minutes / 60) + " h of disk left";
    if (minutes >= 2)
        return juce::String(minutes) + " min of disk left";
    return juce::String(static_cast<int>(seconds)) + " s of disk left";
}
} // namespace

StatusBar::StatusBar(controller::Session& sessionToShow) : session(sessionToShow)
{
    setInterceptsMouseClicks(false, false);
    refresh();
}

void StatusBar::showWarning(const juce::String& text)
{
    warning = text;
    warningUntil = juce::Time::getMillisecondCounter() + 4000;
    refresh();
}

void StatusBar::setCursorVisible(bool shouldBeVisible)
{
    if (line.cursorVisible == shouldBeVisible)
        return;

    line.cursorVisible = shouldBeVisible;
    repaint();
}

void StatusBar::refresh()
{
    using State = controller::RecordingController::State;
    const auto& recording = session.getRecording();
    auto next = line;
    next.message = {};
    next.messageRole = ColourRole::textSecondary;

    switch (recording.getState())
    {
    case State::countingIn:
        next.message = "Count-in...";
        next.messageRole = ColourRole::warning;
        break;
    case State::recording:
    {
        next.message = "Recording";
        if (const auto left = diskTimeLeft(recording.getSecondsLeft()); left.isNotEmpty())
            next.message << "  -  " << left;
        const bool lowSpace =
            recording.getSecondsLeft() >= 0.0 &&
            recording.getSecondsLeft() < controller::RecordingController::lowSpaceWarningSeconds;
        next.messageRole = lowSpace ? ColourRole::warning : ColourRole::error;
        break;
    }
    case State::finishing:
        next.message = "Saving the recording...";
        break;
    case State::idle:
        if (juce::Time::getMillisecondCounter() < warningUntil)
        {
            next.message = warning;
            next.messageRole = ColourRole::warning;
        }
        break;
    }

    const auto& project = session.getProject();
    const auto file = session.getProjectFile();
    next.projectName =
        file != juce::File() ? file.getFileNameWithoutExtension() : juce::String("untitled");
    next.details = juce::String(session.getSampleRate() / 1000.0, 1) + " kHz  " +
                   (project.getRecordingBitsPerSample() == 32
                        ? juce::String("32-bit float")
                        : juce::String(project.getRecordingBitsPerSample()) + "-bit");

    if (next.message != line.message || next.messageRole != line.messageRole ||
        next.details != line.details || next.projectName != line.projectName)
    {
        line = next;
        repaint();
    }
}

void StatusBar::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawStatusBar(g, getLocalBounds().toFloat(), line);
}

} // namespace daw::ui::desktop
