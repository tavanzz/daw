#include "daw/ui/desktop/CoopPanels.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>

namespace daw::ui::desktop
{

namespace
{
using appearance::ColourRole;
using appearance::FontRole;
using Status = coop::Client::Status;

constexpr const char* hostText = "Host";
constexpr const char* joinText = "Join";
constexpr const char* cancelText = "Cancel";
constexpr const char* copyText = "Copy";
constexpr const char* newCodeText = "New Code";
constexpr const char* revokeText = "Revoke";
constexpr const char* playText = "Play for Everyone";
constexpr const char* stopText = "Stop for Everyone";
constexpr const char* followText = "Follow the Shared Transport";
constexpr const char* leaveText = "Leave Session";
constexpr const char* closeSessionText = "Close Session";
constexpr int maximumNameLength = 40;

void styleCaption(juce::Label& label)
{
    ThemedLookAndFeel::setFontRole(label, FontRole::caption);
    ThemedLookAndFeel::setColourRole(label, ColourRole::textSecondary);
    label.setJustificationType(juce::Justification::topLeft);
}

juce::String durationText(double seconds)
{
    const auto minutes = static_cast<int>(seconds / 60.0);
    if (minutes >= 120)
        return juce::String(minutes / 60) + " hours";
    if (minutes >= 2)
        return juce::String(minutes) + " minutes";
    return "a minute";
}
} // namespace

void drawParticipantChip(juce::Graphics& g, const juce::Component& component,
                         juce::Rectangle<float> area, int colour, const juce::String& name)
{
    const auto& look = ThemedLookAndFeel::get(component);
    const auto fill = look.participantColour(colour);
    g.setColour(fill);
    g.fillPath(look.shapeOf(area, CornerSize::small));
    g.setColour(look.textColourOn(fill));
    g.setFont(look.font(FontRole::caption, std::min(area.getHeight() * 0.7f, look.font(FontRole::label).getHeight())));
    g.drawText(name.trim().substring(0, 1).toUpperCase(), area, juce::Justification::centred, false);
}

// CoopStartPanel ---------------------------------------------------------------------------

CoopStartPanel::CoopStartPanel(Mode modeToShow, const controller::CoopSession& coopToWatch,
                               juce::String link, juce::String name, Actions actionsToUse)
    : mode(modeToShow), coop(coopToWatch), actions(std::move(actionsToUse))
{
    const bool hosting = mode == Mode::host;
    ThemedLookAndFeel::setFontRole(headline, FontRole::heading);
    headline.setText(hosting ? "Host a co-op session" : "Join a co-op session", juce::dontSendNotification);
    styleCaption(explanation);
    explanation.setText(
        hosting ? "Paste the server link you got from whoever runs the co-op server. The project "
                  "is copied, with its audio files, into a folder of its own in "
                  "Documents/MICINSTATION/Co-op, where everyone works on it together: your "
                  "original stays as it is. You then get an invite to hand out."
                : "Paste the invite the host sent you. The session's project and its audio files "
                  "come to this computer, into a folder of its own in Documents/MICINSTATION/Co-op. "
                  "The project open now is closed first.",
        juce::dontSendNotification);

    linkLabel.setText(hosting ? "Server link" : "Invite", juce::dontSendNotification);
    nameLabel.setText("Your name", juce::dontSendNotification);
    for (auto* label : {&linkLabel, &nameLabel})
        ThemedLookAndFeel::setFontRole(*label, FontRole::label);
    styleCaption(message);

    linkField.setText(link, false);
    linkField.setTextToShowWhenEmpty(hosting ? "micinstation://server?..." : "micinstation://join?...",
                                     ThemedLookAndFeel::get(*this).colour(ColourRole::textSecondary));
    linkField.setSelectAllWhenFocused(true);
    nameField.setText(name, false);
    nameField.setInputRestrictions(maximumNameLength);
    nameField.onReturnKey = [this] { press(mode == Mode::host ? hostText : joinText); };

    startButton.setButtonText(hosting ? hostText : joinText);
    cancelButton.setButtonText(cancelText);
    for (auto* button : {&startButton, &cancelButton})
        button->onClick = [this, button] { press(button->getButtonText()); };

    for (auto* component : std::initializer_list<juce::Component*>{&headline, &explanation, &linkLabel, &linkField,
                                                                   &nameLabel, &nameField, &message, &startButton,
                                                                   &cancelButton})
        addAndMakeVisible(*component);

    setSize(520, 300);
    lookAndFeelChanged();
}

void CoopStartPanel::lookAndFeelChanged()
{
    const auto& look = ThemedLookAndFeel::get(*this);
    linkField.applyFontToAllText(look.font(FontRole::numeric), true);
    nameField.applyFontToAllText(look.font(FontRole::label), true);
    repaint();
}

CoopStartPanel::~CoopStartPanel()
{
    stopTimer();
}

void CoopStartPanel::press(const juce::String& buttonText)
{
    if (buttonText == cancelText)
    {
        if (waiting)
        {
            stopTimer();
            waiting = false;
            if (actions.giveUp)
                actions.giveUp();
        }
        if (actions.close)
            actions.close();
    }
    else if (!waiting && (buttonText == hostText || buttonText == joinText))
        startSession();
}

void CoopStartPanel::startSession()
{
    const auto link = linkField.getText().trim();
    const auto name = nameField.getText().trim();
    if (mode == Mode::host ? !controller::parseServerLink(link) : !controller::parseInvite(link))
    {
        showProblem(mode == Mode::host
                        ? "That is not a server link: it starts with micinstation://server? and comes "
                          "from whoever runs the co-op server."
                        : "That is not an invite: it starts with micinstation://join? and comes from "
                          "the host of the session.");
        return;
    }
    if (name.isEmpty())
    {
        showProblem("Enter the name the others will see.");
        return;
    }

    waiting = true;
    startButton.setEnabled(false);
    message.setText(mode == Mode::host ? "Copying the project and connecting..." : "Connecting...",
                    juce::dontSendNotification);
    if (!actions.start)
        return;

    juce::Component::SafePointer<CoopStartPanel> self(this);
    actions.start(link, name, [self](juce::Result started) {
        if (self == nullptr)
            return;
        if (started.failed())
            self->showProblem(started.getErrorMessage());
        else
            self->startTimerHz(10);
    });
}

void CoopStartPanel::showProblem(const juce::String& text)
{
    stopTimer();
    waiting = false;
    startButton.setEnabled(true);
    ThemedLookAndFeel::setColourRole(message, ColourRole::warning);
    message.setText(text, juce::dontSendNotification);
}

void CoopStartPanel::timerCallback()
{
    poll();
}

void CoopStartPanel::poll()
{
    const auto* client = coop.findClient();
    if (!waiting || client == nullptr)
        return;
    switch (client->getStatus())
    {
    case Status::connected:
        stopTimer();
        waiting = false;
        if (actions.connected)
            actions.connected();
        break;
    case Status::ended:
        showProblem(client->getProblem());
        break;
    case Status::reconnecting:
        message.setText("The server does not answer yet; trying again...", juce::dontSendNotification);
        break;
    case Status::idle:
    case Status::connecting:
        break;
    }
}

void CoopStartPanel::paint(juce::Graphics& g)
{
    g.fillAll(ThemedLookAndFeel::get(*this).colour(ColourRole::panel));
}

void CoopStartPanel::resized()
{
    auto area = getLocalBounds().reduced(20);
    headline.setBounds(area.removeFromTop(28));
    explanation.setBounds(area.removeFromTop(72));
    area.removeFromTop(8);
    const auto field = [&area](juce::Label& label, juce::TextEditor& editor) {
        auto row = area.removeFromTop(28);
        label.setBounds(row.removeFromLeft(100));
        editor.setBounds(row);
        area.removeFromTop(8);
    };
    field(linkLabel, linkField);
    field(nameLabel, nameField);
    auto buttons = area.removeFromBottom(30);
    cancelButton.setBounds(buttons.removeFromRight(100));
    buttons.removeFromRight(8);
    startButton.setBounds(buttons.removeFromRight(100));
    message.setBounds(area);
}

// CoopSessionPanel -------------------------------------------------------------------------

/** One participant: their chip, name and what they are doing; the host's buttons. */
class CoopSessionPanel::Row final : public juce::Component
{
public:
    static constexpr int height = 30;

    Row(CoopSessionPanel& panelToTell, coop::ParticipantInfo infoToShow, bool isYou, bool youHost)
        : panel(panelToTell), info(std::move(infoToShow)), you(isYou)
    {
        ThemedLookAndFeel::setFontRole(name, FontRole::label);
        name.setText(info.name, juce::dontSendNotification);
        styleCaption(details);
        details.setJustificationType(juce::Justification::centredLeft);
        details.setText(describe(), juce::dontSendNotification);
        name.setInterceptsMouseClicks(false, false);
        details.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(name);
        addAndMakeVisible(details);

        if (youHost && !you)
        {
            handOver.setButtonText("Make Host");
            remove.setButtonText("Remove");
            handOver.onClick = [this] { panel.press("Make " + info.name + " Host"); };
            remove.onClick = [this] { panel.press("Remove " + info.name); };
            addAndMakeVisible(handOver);
            addAndMakeVisible(remove);
        }
    }

    [[nodiscard]] juce::String line() const
    {
        const auto text = describe();
        return info.name + (text.isNotEmpty() ? " (" + text + ")" : juce::String());
    }

    [[nodiscard]] juce::StringArray buttonTexts() const
    {
        if (!remove.isVisible())
            return {};
        return {"Make " + info.name + " Host", "Remove " + info.name};
    }

    void paint(juce::Graphics& g) override
    {
        drawParticipantChip(g, *this, juce::Rectangle<float>(4.0f, 5.0f, 20.0f, 20.0f), info.colour, info.name);
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedLeft(32);
        if (remove.isVisible())
        {
            remove.setBounds(area.removeFromRight(76).reduced(0, 3));
            area.removeFromRight(6);
            handOver.setBounds(area.removeFromRight(90).reduced(0, 3));
        }
        name.setBounds(area.removeFromLeft(std::min(180, area.getWidth() / 2)));
        details.setBounds(area);
    }

private:
    [[nodiscard]] juce::String describe() const
    {
        juce::StringArray parts;
        if (you)
            parts.add("you");
        if (info.host)
            parts.add("host");
        if (info.recording)
            parts.add("recording");
        if (!info.connected)
            parts.add("away");
        return parts.joinIntoString(", ");
    }

    CoopSessionPanel& panel;
    coop::ParticipantInfo info;
    bool you = false;
    juce::Label name, details;
    juce::TextButton handOver, remove;
};

CoopSessionPanel::CoopSessionPanel(controller::CoopSession& coopToShow, Actions actionsToUse)
    : coop(coopToShow), actions(std::move(actionsToUse))
{
    ThemedLookAndFeel::setFontRole(headline, FontRole::heading);
    ThemedLookAndFeel::setFontRole(status, FontRole::label);
    ThemedLookAndFeel::setFontRole(inviteLabel, FontRole::heading);
    ThemedLookAndFeel::setFontRole(peopleLabel, FontRole::heading);
    inviteLabel.setText("Invite", juce::dontSendNotification);
    peopleLabel.setText("Participants", juce::dontSendNotification);
    styleCaption(inviteNote);

    inviteField.setReadOnly(true);
    inviteField.setCaretVisible(false);
    inviteField.setSelectAllWhenFocused(true);

    copyButton.setButtonText(copyText);
    newCodeButton.setButtonText(newCodeText);
    revokeButton.setButtonText(revokeText);
    playButton.setButtonText(playText);
    stopButton.setButtonText(stopText);
    leaveButton.setButtonText(leaveText);
    closeButton.setButtonText(closeSessionText);
    followButton.setButtonText(followText);
    followButton.setToggleState(coop.isFollowing(), juce::dontSendNotification);
    followButton.onClick = [this] { press(followText); };
    for (auto* button : {&copyButton, &newCodeButton, &revokeButton, &playButton, &stopButton, &leaveButton,
                         &closeButton})
        button->onClick = [this, button] { press(button->getButtonText()); };

    rowsView.setViewedComponent(&rowsHolder, false);
    rowsView.setScrollBarsShown(true, false);

    for (auto* component : std::initializer_list<juce::Component*>{
             &headline, &status, &inviteLabel, &inviteField, &inviteNote, &copyButton, &newCodeButton, &revokeButton,
             &peopleLabel, &rowsView, &playButton, &stopButton, &followButton, &leaveButton, &closeButton})
        addAndMakeVisible(*component);

    setSize(560, 480);
    lookAndFeelChanged();
    refresh();
    startTimerHz(4);
}

CoopSessionPanel::~CoopSessionPanel()
{
    stopTimer();
}

void CoopSessionPanel::timerCallback()
{
    const auto* client = coop.findClient();
    if (client != nullptr && client->getRevision() != shownRevision)
        refresh();
    else if (client != nullptr && client->isHost())
    {
        // The invite's time left goes down on its own.
        if (const auto left = coop.getInviteSecondsLeft())
            inviteNote.setText("Anyone with this link can join for the next " + durationText(*left)
                                   + ". New Code makes another one (the old one stops working).",
                               juce::dontSendNotification);
    }
}

void CoopSessionPanel::refresh()
{
    const auto* client = coop.findClient();
    shownRevision = client != nullptr ? client->getRevision() : 0;

    const auto title = coop.getTitle();
    headline.setText(title.isNotEmpty() ? "Co-op: " + title : juce::String("Co-op session"), juce::dontSendNotification);

    auto statusRole = ColourRole::text;
    juce::String statusText = "Not in a session.";
    if (client != nullptr)
        switch (client->getStatus())
        {
        case Status::idle:
            break;
        case Status::connecting:
            statusText = "Connecting...";
            statusRole = ColourRole::warning;
            break;
        case Status::connected:
            statusText = "Connected to " + coop::addressText(coop.getServer()) + ".";
            break;
        case Status::reconnecting:
            statusText = "Reconnecting... You can go on with what you were editing for a while.";
            statusRole = ColourRole::warning;
            break;
        case Status::ended:
            statusText = client->getProblem() + " The copy stays, as an ordinary project.";
            statusRole = ColourRole::error;
            break;
        }
    ThemedLookAndFeel::setColourRole(status, statusRole);
    status.setText(statusText, juce::dontSendNotification);

    const bool active = coop.isActive();
    const bool host = active && client != nullptr && client->isHost();
    const auto invite = coop.getInviteLink();
    inviteField.setText(invite, false);
    copyButton.setEnabled(invite.isNotEmpty());
    newCodeButton.setVisible(host);
    revokeButton.setVisible(host);
    revokeButton.setEnabled(invite.isNotEmpty());
    if (!active)
        inviteNote.setText({}, juce::dontSendNotification);
    else if (!host)
        inviteNote.setText("Only the host hands out invites.", juce::dontSendNotification);
    else if (invite.isEmpty())
        inviteNote.setText("No invite: nobody new can join. New Code makes one.", juce::dontSendNotification);
    else if (const auto left = coop.getInviteSecondsLeft())
        inviteNote.setText("Anyone with this link can join for the next " + durationText(*left)
                               + ". New Code makes another one (the old one stops working).",
                           juce::dontSendNotification);
    inviteField.setVisible(host || invite.isNotEmpty());
    copyButton.setVisible(host || invite.isNotEmpty());

    rows.clear();
    if (client != nullptr)
        for (const auto& participant : client->getParticipants())
        {
            rows.push_back(std::make_unique<Row>(*this, participant, participant.number == client->getParticipant(),
                                                 host));
            rowsHolder.addAndMakeVisible(*rows.back());
        }

    playButton.setEnabled(active);
    stopButton.setEnabled(active);
    followButton.setToggleState(coop.isFollowing(), juce::dontSendNotification);
    leaveButton.setButtonText(active ? leaveText : "Close");
    closeButton.setVisible(host);
    resized();
}

juce::StringArray CoopSessionPanel::getParticipantLines() const
{
    juce::StringArray lines;
    for (const auto& row : rows)
        lines.add(row->line());
    return lines;
}

juce::StringArray CoopSessionPanel::getButtonTexts() const
{
    juce::StringArray texts;
    for (const auto* button : {&copyButton, &newCodeButton, &revokeButton})
        if (button->isVisible())
            texts.add(button->getButtonText());
    for (const auto& row : rows)
        texts.addArray(row->buttonTexts());
    for (const auto* button : {&playButton, &stopButton, &leaveButton, &closeButton})
        if (button->isVisible())
            texts.add(button->getButtonText());
    return texts;
}

void CoopSessionPanel::press(const juce::String& text)
{
    auto* client = coop.findClient() != nullptr && coop.isActive() ? &coop.getClient() : nullptr;
    using Kind = coop::HostCommand::Kind;

    if (text == copyText)
        juce::SystemClipboard::copyTextToClipboard(coop.getInviteLink());
    else if (text == newCodeText && client != nullptr)
        client->sendHostCommand({Kind::newInvite, 0});
    else if (text == revokeText && client != nullptr)
        client->sendHostCommand({Kind::revokeInvite, 0});
    else if (text == playText)
        coop.playForEveryone(coop.getSession().getPlayhead());
    else if (text == stopText)
        coop.stopForEveryone();
    else if (text == followText)
    {
        coop.setFollowing(followButton.getToggleState());
        if (actions.follow)
            actions.follow(coop.isFollowing());
    }
    else if (text == leaveText || text == "Close")
    {
        if (actions.leave)
            actions.leave();
    }
    else if (text == closeSessionText)
    {
        if (actions.closeSession)
            actions.closeSession();
    }
    else if (client != nullptr)
        for (const auto& participant : client->getParticipants())
        {
            if (text == "Remove " + participant.name)
                client->sendHostCommand({Kind::remove, participant.number});
            else if (text == "Make " + participant.name + " Host")
                client->sendHostCommand({Kind::handOver, participant.number});
        }
}

void CoopSessionPanel::lookAndFeelChanged()
{
    inviteField.applyFontToAllText(ThemedLookAndFeel::get(*this).font(FontRole::numeric), true);
    repaint();
}

void CoopSessionPanel::paint(juce::Graphics& g)
{
    g.fillAll(ThemedLookAndFeel::get(*this).colour(ColourRole::panel));
}

void CoopSessionPanel::resized()
{
    auto area = getLocalBounds().reduced(20);
    headline.setBounds(area.removeFromTop(28));
    status.setBounds(area.removeFromTop(40));
    area.removeFromTop(6);

    inviteLabel.setBounds(area.removeFromTop(24));
    if (inviteField.isVisible())
    {
        auto row = area.removeFromTop(28);
        for (auto* button : {&revokeButton, &newCodeButton, &copyButton})
            if (button->isVisible())
            {
                button->setBounds(row.removeFromRight(button == &newCodeButton ? 90 : 70));
                row.removeFromRight(6);
            }
        inviteField.setBounds(row);
    }
    inviteNote.setBounds(area.removeFromTop(36));
    area.removeFromTop(6);

    auto bottom = area.removeFromBottom(30);
    closeButton.setBounds(bottom.removeFromRight(closeButton.isVisible() ? 120 : 0));
    bottom.removeFromRight(closeButton.isVisible() ? 6 : 0);
    leaveButton.setBounds(bottom.removeFromRight(120));
    area.removeFromBottom(8);
    auto transport = area.removeFromBottom(30);
    playButton.setBounds(transport.removeFromLeft(140));
    transport.removeFromLeft(6);
    stopButton.setBounds(transport.removeFromLeft(140));
    transport.removeFromLeft(10);
    followButton.setBounds(transport);
    area.removeFromBottom(8);

    peopleLabel.setBounds(area.removeFromTop(24));
    rowsView.setBounds(area);
    const int width = rowsView.getMaximumVisibleWidth();
    rowsHolder.setSize(width, static_cast<int>(rows.size()) * Row::height);
    int y = 0;
    for (auto& row : rows)
    {
        row->setBounds(0, y, width, Row::height);
        y += Row::height;
    }
}

} // namespace daw::ui::desktop
