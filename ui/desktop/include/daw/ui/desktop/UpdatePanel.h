#pragma once

#include "daw/services/UpdateChecker.h"
#include "daw/ui/desktop/AppServices.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/**
    The update panel: what the update check found (or why it failed), the new version's
    release notes, and what to do. "Update Now" downloads, verifies and installs in one
    click (the app then offers to save, quits and starts the new version); "Skip This
    Version" stops offering it at launch; "Later" closes. A copy that cannot replace
    itself (AppServices::updateInstallProblem) offers the download page instead.

    It shows services::UpdateChecker's status, polled; every decision is the checker's.
*/
class UpdatePanel final : public juce::Component, private juce::Timer
{
public:
    UpdatePanel(AppServices services, std::function<void()> close);
    ~UpdatePanel() override;

    // For tests.
    [[nodiscard]] juce::String getHeadline() const { return headline.getText(); }
    [[nodiscard]] juce::StringArray getButtonTexts() const;
    void press(const juce::String& buttonText);
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void timerCallback() override;
    void updateNow();
    void install();

    AppServices services;
    std::function<void()> closePanel;
    int shownRevision = -1;
    bool installWhenReady = false;
    bool installing = false;
    juce::String problem; ///< from installing

    juce::Label headline, message;
    juce::TextEditor notes;
    double progress = 0.0;
    juce::ProgressBar progressBar{progress};
    juce::TextButton primary, secondary, tertiary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdatePanel)
};

} // namespace daw::ui::desktop
