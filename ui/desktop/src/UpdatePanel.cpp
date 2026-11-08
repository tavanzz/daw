#include "daw/ui/desktop/UpdatePanel.h"

#include "daw/services/BuildInfo.h"
#include "daw/services/UserSettings.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

namespace
{
using appearance::ColourRole;
using appearance::FontRole;
using State = services::UpdateChecker::State;

constexpr const char* updateNowText = "Update Now";
constexpr const char* downloadPageText = "Open Download Page";
constexpr const char* skipText = "Skip This Version";
constexpr const char* laterText = "Later";
constexpr const char* closeText = "Close";
constexpr const char* cancelText = "Cancel";
constexpr const char* tryAgainText = "Try Again";
} // namespace

UpdatePanel::UpdatePanel(AppServices servicesToUse, std::function<void()> close)
    : services(std::move(servicesToUse))
    , closePanel(std::move(close))
{
    ThemedLookAndFeel::setFontRole(headline, FontRole::heading);
    message.setJustificationType(juce::Justification::topLeft);
    ThemedLookAndFeel::setFontRole(message, FontRole::caption);
    ThemedLookAndFeel::setColourRole(message, ColourRole::textSecondary);

    notes.setMultiLine(true, true);
    notes.setReadOnly(true);
    notes.setScrollbarsShown(true);
    notes.setCaretVisible(false);

    for (auto* button : {&primary, &secondary, &tertiary})
        button->onClick = [this, button] { press(button->getButtonText()); };

    for (auto* component : std::initializer_list<juce::Component*>{&headline, &message, &notes, &progressBar,
                                                                   &primary, &secondary, &tertiary})
        addChildComponent(*component);
    headline.setVisible(true);
    message.setVisible(true);

    setSize(560, 460);
    lookAndFeelChanged();
    refresh();
    startTimerHz(10);
}

UpdatePanel::~UpdatePanel()
{
    stopTimer();
}

juce::StringArray UpdatePanel::getButtonTexts() const
{
    juce::StringArray texts;
    for (const auto* button : {&primary, &secondary, &tertiary})
        if (button->isVisible())
            texts.add(button->getButtonText());
    return texts;
}

void UpdatePanel::timerCallback()
{
    if (services.updates != nullptr && services.updates->getRevision() != shownRevision)
        refresh();
}

void UpdatePanel::refresh()
{
    const auto& build = services::buildInfo();
    const auto status = services.updates != nullptr ? services.updates->getStatus() : services::UpdateChecker::Status{};
    shownRevision = services.updates != nullptr ? services.updates->getRevision() : 0;

    const auto setButtons = [this](juce::StringArray texts) {
        juce::TextButton* buttons[] = {&primary, &secondary, &tertiary};
        for (int index = 0; index < 3; ++index)
        {
            buttons[index]->setVisible(index < texts.size());
            buttons[index]->setButtonText(texts[index]);
        }
    };

    notes.setVisible(false);
    progressBar.setVisible(false);
    juce::String text;

    if (installing)
    {
        headline.setText("Installing the update...", juce::dontSendNotification);
        text = build.productName + " will quit and start again.";
        setButtons({});
    }
    else if (problem.isNotEmpty())
    {
        headline.setText("The update cannot be installed", juce::dontSendNotification);
        text = problem;
        setButtons({downloadPageText, closeText});
    }
    else
        switch (status.state)
        {
        case State::idle:
        case State::checking:
            headline.setText("Looking for updates...", juce::dontSendNotification);
            setButtons({closeText});
            break;
        case State::upToDate:
            headline.setText(build.productName + " " + build.version + " is up to date.", juce::dontSendNotification);
            setButtons({closeText});
            break;
        case State::failed:
            headline.setText("Could not check for updates", juce::dontSendNotification);
            text = status.problem.substring(0, 1).toUpperCase() + status.problem.substring(1) + ".";
            setButtons({tryAgainText, downloadPageText, closeText});
            break;
        case State::available:
        case State::downloading:
        case State::ready:
        {
            const auto newVersion = status.release ? status.release->version.toString() : juce::String();
            headline.setText(build.productName + " " + newVersion + " is available", juce::dontSendNotification);
            text = "You have " + build.version + ".";
            notes.setText(status.release ? status.release->notes : juce::String(), false);
            notes.setVisible(true);

            if (status.state == State::downloading)
            {
                progress = status.progress;
                progressBar.setVisible(true);
                text << " Downloading and checking the update...";
                setButtons({cancelText});
            }
            else if (status.state == State::ready && installWhenReady)
            {
                // Verified: installed at once, after this refresh.
                installWhenReady = false;
                setButtons({});
                juce::Component::SafePointer<UpdatePanel> self(this);
                juce::MessageManager::callAsync([self] {
                    if (self != nullptr)
                        self->install();
                });
            }
            else if (!status.installable || services.updateInstallProblem.isNotEmpty() || !services.installUpdate)
            {
                if (services.updateInstallProblem.isNotEmpty())
                    text << " This copy cannot update itself: " << services.updateInstallProblem << ".";
                setButtons({downloadPageText, skipText, laterText});
            }
            else
            {
                setButtons({updateNowText, skipText, laterText});
            }
            break;
        }
        }

    message.setText(text, juce::dontSendNotification);
    resized();
}

void UpdatePanel::press(const juce::String& text)
{
    if (text == updateNowText)
        updateNow();
    else if (text == downloadPageText)
    {
        const auto status = services.updates != nullptr ? services.updates->getStatus() : services::UpdateChecker::Status{};
        juce::URL(status.release && status.release->releaseUrl.isNotEmpty()
                      ? status.release->releaseUrl
                      : (services.updates != nullptr ? services.updates->getReleasesPage() : juce::String()))
            .launchInDefaultBrowser();
    }
    else if (text == skipText)
    {
        if (services.settings != nullptr && services.updates != nullptr)
            if (const auto status = services.updates->getStatus(); status.release)
            {
                auto preferences = services.settings->getUpdatePreferences();
                preferences.skippedVersion = status.release->version.toString();
                services.settings->setUpdatePreferences(preferences);
                services.settings->save();
            }
        if (closePanel)
            closePanel();
    }
    else if (text == cancelText)
    {
        installWhenReady = false;
        if (services.updates != nullptr)
            services.updates->cancel();
    }
    else if (text == tryAgainText)
    {
        if (services.updates != nullptr)
            services.updates->check(services.settings != nullptr ? services.settings->getUpdatePreferences()
                                                                 : services::UpdatePreferences{},
                                    true);
    }
    else if (text == laterText || text == closeText)
    {
        if (closePanel)
            closePanel();
    }
}

void UpdatePanel::updateNow()
{
    if (services.updates == nullptr)
        return;
    if (services.updates->getStatus().state == State::ready)
    {
        install();
        return;
    }
    installWhenReady = true;
    services.updates->download();
    refresh();
}

void UpdatePanel::install()
{
    installWhenReady = false;
    if (!services.installUpdate)
        return;
    installing = true;
    refresh();
    juce::Component::SafePointer<UpdatePanel> self(this);
    services.installUpdate([self](const juce::String& why) {
        if (self == nullptr)
            return;
        self->installing = false;
        self->problem = why;
        self->refresh();
    });
}

void UpdatePanel::lookAndFeelChanged()
{
    if (auto* look = dynamic_cast<ThemedLookAndFeel*>(&getLookAndFeel()))
        notes.applyFontToAllText(look->font(FontRole::caption), true);
}

void UpdatePanel::paint(juce::Graphics& g)
{
    g.fillAll(ThemedLookAndFeel::get(*this).colour(ColourRole::panel));
}

void UpdatePanel::resized()
{
    auto area = getLocalBounds().reduced(20);
    headline.setBounds(area.removeFromTop(30));
    message.setBounds(area.removeFromTop(48));
    auto buttons = area.removeFromBottom(30);
    for (auto* button : {&tertiary, &secondary, &primary})
        if (button->isVisible())
        {
            button->setBounds(buttons.removeFromRight(150));
            buttons.removeFromRight(8);
        }
    area.removeFromBottom(12);
    if (progressBar.isVisible())
    {
        progressBar.setBounds(area.removeFromBottom(18));
        area.removeFromBottom(8);
    }
    notes.setBounds(area);
}

} // namespace daw::ui::desktop
