#include "daw/ui/desktop/FirstLaunchPanel.h"

#include "daw/services/BuildInfo.h"
#include "daw/ui/desktop/ThemePreviewsPanel.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

namespace
{
using appearance::ColourRole;
using appearance::FontRole;

constexpr int consentGroup = 4301;
} // namespace

FirstLaunchPanel::FirstLaunchPanel(services::FirstLaunchPages toShow, juce::AudioDeviceManager& devices,
                                   ThemeManager& themes, std::function<void(std::optional<bool>)> finished)
    : onFinished(std::move(finished))
{
    if (toShow.audioDevice)
        pages.push_back(Page::audioDevice);
    if (toShow.theme)
        pages.push_back(Page::theme);
    if (toShow.crashReports)
        pages.push_back(Page::crashReports);

    ThemedLookAndFeel::setFontRole(title, FontRole::title);
    explanation.setJustificationType(juce::Justification::topLeft);
    ThemedLookAndFeel::setColourRole(explanation, ColourRole::textSecondary);
    ThemedLookAndFeel::setFontRole(step, FontRole::caption);
    ThemedLookAndFeel::setColourRole(step, ColourRole::textSecondary);

    // Outputs only: inputs would ask for the microphone, which waits until a track needs one.
    deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(devices, 0, 0, 1, 2, false, false, false,
                                                                          false);
    themePreviews = std::make_unique<ThemePreviewsPanel>(themes);

    for (auto* button : {&sendReports, &dontSend})
    {
        button->setRadioGroupId(consentGroup);
        button->setClickingTogglesState(true);
        button->onClick = [this, button] {
            if (button->getToggleState())
                chooseCrashReports(button == &sendReports);
        };
    }
    privacy.setButtonText("What a report contains, in full");
    privacy.setURL(juce::URL(services::buildInfo().repositoryUrl + "/blob/main/docs/PRIVACY.md"));
    privacy.setJustificationType(juce::Justification::centredLeft);

    backButton.onClick = [this] { back(); };
    nextButton.onClick = [this] { next(); };

    for (auto* component : std::initializer_list<juce::Component*>{&title, &explanation, &step, &backButton,
                                                                   &nextButton})
        addAndMakeVisible(*component);
    for (auto* component : std::initializer_list<juce::Component*>{deviceSelector.get(), themePreviews.get(),
                                                                   &sendReports, &dontSend, &privacy})
        addChildComponent(*component);

    // Room for the previews below the title and the explanation.
    setSize(std::max(640, themePreviews->getWidth() + 40), std::max(560, themePreviews->getHeight() + 180));
    showPage(0);
}

FirstLaunchPanel::~FirstLaunchPanel() = default;

bool FirstLaunchPanel::canGoOn() const
{
    return page >= getNumPages() || pages[static_cast<size_t>(page)] != Page::crashReports ||
           crashReportAnswer.has_value();
}

void FirstLaunchPanel::chooseCrashReports(bool send)
{
    crashReportAnswer = send;
    (send ? sendReports : dontSend).setToggleState(true, juce::dontSendNotification);
    nextButton.setEnabled(canGoOn());
}

void FirstLaunchPanel::next()
{
    if (!canGoOn())
        return;
    if (page + 1 < getNumPages())
    {
        showPage(page + 1);
        return;
    }
    if (onFinished)
        onFinished(crashReportAnswer);
}

void FirstLaunchPanel::back()
{
    if (page > 0)
        showPage(page - 1);
}

void FirstLaunchPanel::showPage(int index)
{
    page = juce::jlimit(0, std::max(0, getNumPages() - 1), index);
    const auto& build = services::buildInfo();
    const auto shown = pages.empty() ? Page::audioDevice : pages[static_cast<size_t>(page)];

    deviceSelector->setVisible(shown == Page::audioDevice);
    themePreviews->setVisible(shown == Page::theme);
    for (auto* component : std::initializer_list<juce::Component*>{&sendReports, &dontSend, &privacy})
        component->setVisible(shown == Page::crashReports);

    switch (shown)
    {
    case Page::audioDevice:
        title.setText("Where " + build.productName + " plays", juce::dontSendNotification);
        explanation.setText("Choose your audio interface or your computer's output, and press Test to hear a "
                            "tone. You can change it later in File > Audio Settings, where the inputs for "
                            "recording are too.",
                            juce::dontSendNotification);
        break;
    case Page::theme:
        title.setText("Pick a look", juce::dontSendNotification);
        explanation.setText("Click a theme to use it. View > Theme changes it at any time.",
                            juce::dontSendNotification);
        break;
    case Page::crashReports:
        title.setText("Crash reports", juce::dontSendNotification);
        explanation.setText(
            "If " + build.productName + " crashes, it can send a report so that the cause can be found and fixed. "
            "A report holds the app's and the system's versions and where in the program the crash happened, "
            "including the plugin that was running. Never your name, your computer's name, your projects or your "
            "audio. Reports go to Sentry, in the European Union. A copy of each report stays in the log folder "
            "either way.\n\nYou can change your answer at any time in Help > Send Crash Reports. "
            + build.productName + " also checks GitHub for new versions when it starts (Help > Check for Updates "
            "at Launch).",
            juce::dontSendNotification);
        break;
    }

    step.setText(juce::String(page + 1) + " of " + juce::String(getNumPages()), juce::dontSendNotification);
    backButton.setVisible(page > 0);
    nextButton.setButtonText(page + 1 < getNumPages() ? "Next" : "Start");
    nextButton.setEnabled(canGoOn());
    resized();
}

void FirstLaunchPanel::paint(juce::Graphics& g)
{
    g.fillAll(ThemedLookAndFeel::get(*this).colour(ColourRole::panel));
}

void FirstLaunchPanel::resized()
{
    auto area = getLocalBounds().reduced(20);
    title.setBounds(area.removeFromTop(40));
    const bool consent = sendReports.isVisible();
    explanation.setBounds(area.removeFromTop(consent ? 190 : 56));
    auto buttons = area.removeFromBottom(30);
    nextButton.setBounds(buttons.removeFromRight(120));
    buttons.removeFromRight(8);
    backButton.setBounds(buttons.removeFromRight(120));
    step.setBounds(buttons);
    area.removeFromBottom(10);

    deviceSelector->setBounds(area);
    themePreviews->setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), themePreviews->getWidth()),
                                                        std::min(area.getHeight(), themePreviews->getHeight())));
    sendReports.setBounds(area.removeFromTop(28).withWidth(260));
    dontSend.setBounds(area.removeFromTop(28).withWidth(260));
    area.removeFromTop(10);
    privacy.setBounds(area.removeFromTop(24).withWidth(320));
}

} // namespace daw::ui::desktop
