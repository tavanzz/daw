#pragma once

#include "daw/services/UserSettings.h"
#include "daw/ui/desktop/ThemeManager.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

namespace daw::ui::desktop
{

class ThemePreviewsPanel;

/**
    The first launch: up to three pages, only those the user has not decided yet
    (services::UserSettings::getFirstLaunchPages()): where the app plays (the output
    device; no inputs, so nothing asks for the microphone), the theme (the three
    previews, applied as they are clicked), and whether crash reports may be sent
    (neither answer chosen until the user picks one). Nothing else.
*/
class FirstLaunchPanel final : public juce::Component
{
public:
    /** `finished(sendCrashReports)`: the answer, or nothing if that page was not shown. */
    FirstLaunchPanel(services::FirstLaunchPages pages, juce::AudioDeviceManager& devices, ThemeManager& themes,
                     std::function<void(std::optional<bool>)> finished);
    ~FirstLaunchPanel() override;

    // For tests.
    [[nodiscard]] int getNumPages() const noexcept { return static_cast<int>(pages.size()); }
    [[nodiscard]] int getPageIndex() const noexcept { return page; }
    [[nodiscard]] juce::String getPageTitle() const { return title.getText(); }
    [[nodiscard]] bool canGoOn() const;
    void next();
    void back();
    void chooseCrashReports(bool send);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    enum class Page
    {
        audioDevice,
        theme,
        crashReports
    };

    void showPage(int index);

    std::vector<Page> pages;
    int page = 0;
    std::optional<bool> crashReportAnswer;
    std::function<void(std::optional<bool>)> onFinished;

    juce::Label title, explanation, step;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    std::unique_ptr<ThemePreviewsPanel> themePreviews;
    juce::ToggleButton sendReports{"Send crash reports"}, dontSend{"Don't send"};
    juce::HyperlinkButton privacy;
    juce::TextButton backButton{"Back"}, nextButton{"Next"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FirstLaunchPanel)
};

} // namespace daw::ui::desktop
