#pragma once

#include "daw/controller/CoopSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/**
    Co-op > Host a Session... or Join a Session...: a link (the server link to host with,
    or the invite to join with) and the name the others will see. "Host" or "Join" hands
    both to onStart (which asks to save, then starts the controller::CoopSession), then the
    panel waits for the server's answer: onConnected once in the session, or the reason it
    refused, and the fields again.
*/
class CoopStartPanel final : public juce::Component, private juce::Timer
{
public:
    enum class Mode
    {
        host,
        join
    };

    struct Actions
    {
        /** Starts hosting or joining; the result says why it could not start. */
        std::function<void(const juce::String& link, const juce::String& name,
                           std::function<void(juce::Result)> started)>
            start;
        std::function<void()> connected;
        /** Cancel while waiting for the server: stop trying (the copy stays). */
        std::function<void()> giveUp;
        std::function<void()> close;
    };

    CoopStartPanel(Mode mode, const controller::CoopSession& coop, juce::String link,
                   juce::String name, Actions actions);
    ~CoopStartPanel() override;

    // For tests.
    void setLink(const juce::String& text) { linkField.setText(text, false); }
    void setParticipantName(const juce::String& text) { nameField.setText(text, false); }
    [[nodiscard]] juce::String getMessage() const { return message.getText(); }
    [[nodiscard]] bool isWaiting() const noexcept { return waiting; }
    void press(const juce::String& buttonText);
    /** Looks at the session's state (the panel's timer does, while it waits). */
    void poll();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void timerCallback() override;
    void startSession();
    void showProblem(const juce::String& text);

    Mode mode;
    const controller::CoopSession& coop;
    Actions actions;
    bool waiting = false;

    juce::Label headline, explanation, linkLabel, nameLabel, message;
    juce::TextEditor linkField, nameField;
    juce::TextButton startButton, cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoopStartPanel)
};

/**
    Co-op > Session...: the session as this copy sees it. Its state (connected,
    reconnecting, ended and why), the invite to hand out (the host's: Copy, New Code,
    Revoke), the participants in their colours (who is here, who hosts, who records; the
    host can remove them or hand the role over), Play and Stop for Everyone, Follow, and
    Leave or Close Session (both confirmed by the actions). Refreshed from the client's
    revision.
*/
class CoopSessionPanel final : public juce::Component, private juce::Timer
{
public:
    struct Actions
    {
        std::function<void()> leave;        ///< asks, then leaves
        std::function<void()> closeSession; ///< asks, then closes it for everyone
        std::function<void(bool)> follow;   ///< remembers the choice
    };

    CoopSessionPanel(controller::CoopSession& coop, Actions actions);
    ~CoopSessionPanel() override;

    // For tests.
    void refresh();
    [[nodiscard]] juce::String getHeadline() const { return headline.getText(); }
    [[nodiscard]] juce::String getStatus() const { return status.getText(); }
    [[nodiscard]] juce::String getInvite() const { return inviteField.getText(); }
    /** One line per participant, e.g. "Ann (you, host)". */
    [[nodiscard]] juce::StringArray getParticipantLines() const;
    /** The buttons shown, in order (participant rows' included, e.g. "Remove Marco"). */
    [[nodiscard]] juce::StringArray getButtonTexts() const;
    void press(const juce::String& buttonText);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    class Row;

    void timerCallback() override;

    controller::CoopSession& coop;
    Actions actions;
    std::uint64_t shownRevision = ~std::uint64_t{0};

    juce::Label headline, status, inviteLabel, inviteNote, peopleLabel;
    juce::TextEditor inviteField;
    juce::TextButton copyButton, newCodeButton, revokeButton;
    juce::TextButton playButton, stopButton, leaveButton, closeButton;
    juce::ToggleButton followButton;
    juce::Component rowsHolder;
    juce::Viewport rowsView;
    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoopSessionPanel)
};

/** A participant's colour mark with their initial, as the co-op views draw it. */
void drawParticipantChip(juce::Graphics& g, const juce::Component& component,
                         juce::Rectangle<float> area, int colour, const juce::String& name);

} // namespace daw::ui::desktop
