#include "daw/ui/desktop/TransportBar.h"

#include "daw/controller/TimelineViewport.h"

#include <cmath>
#include <initializer_list>

namespace daw::ui::desktop
{

namespace
{

constexpr model::GridDivision gridChoices[] = {
    model::GridDivision::automatic, model::GridDivision::bar,       model::GridDivision::half,
    model::GridDivision::quarter,   model::GridDivision::eighth,    model::GridDivision::sixteenth,
    model::GridDivision::thirtySecond};

void styleDisplay(juce::Label& label, float height)
{
    label.setFont(juce::FontOptions(height));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1e1e1e));
    label.setColour(juce::Label::outlineColourId, juce::Colour(0xff3a3a3a));
}

} // namespace

TransportBar::TransportBar(controller::Session& sessionToControl)
    : session(sessionToControl)
{
    for (auto* button : std::initializer_list<juce::Button*>{
             &playButton, &pauseButton, &stopButton, &loopButton, &snapButton, &recordButton,
             &metronomeButton, &countInButton, &punchButton})
    {
        // Space and Return are transport shortcuts; a focused button must not eat them.
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }

    playButton.setTooltip("Play (Space)");
    pauseButton.setTooltip("Pause (Space)");
    stopButton.setTooltip("Stop: back to where playback started; again: to the start (Return)");
    loopButton.setTooltip("Loop the loop region, or the whole project if there is none (L)");
    snapButton.setTooltip("Snap clips and edits to the grid (N); hold Cmd while dragging to "
                          "ignore it");

    for (auto* button : std::initializer_list<juce::TextButton*>{
             &loopButton, &snapButton, &metronomeButton, &countInButton, &punchButton})
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2e7d32));
    recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd32f2f));

    recordButton.setTooltip("Record the armed tracks from the playhead; again: stop (R)");
    metronomeButton.setTooltip("Metronome while playing and recording (K); right-click for "
                               "its volume");
    countInButton.setTooltip("Count in before recording (Shift+K); right-click for 1 or 2 bars");
    punchButton.setTooltip("Punch in/out: record only inside the loop region (P)");

    recordButton.onClick = [this]
    {
        if (onRecord)
            onRecord();
    };
    metronomeButton.onClick = [this]
    {
        session.setMetronomeEnabled(!session.getProject().isMetronomeEnabled());
    };
    countInButton.onClick = [this]
    {
        session.setCountInEnabled(!session.getProject().isCountInEnabled());
    };
    punchButton.onClick = [this]
    {
        session.setPunchEnabled(!session.getProject().isPunchEnabled());
    };

    metronomeButton.onRightClick = [this]
    {
        juce::PopupMenu menu;
        const auto volume = session.getProject().getMetronomeVolume();
        for (const int decibels : {0, -6, -12, -18, -24})
        {
            const auto gain = juce::Decibels::decibelsToGain(static_cast<double>(decibels));
            menu.addItem("Volume " + juce::String(decibels) + " dB", true,
                         std::abs(gain - volume) < 0.01,
                         [this, gain] { session.setMetronomeVolume(gain); });
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&metronomeButton));
    };
    countInButton.onRightClick = [this]
    {
        juce::PopupMenu menu;
        for (int bars = 1; bars <= model::Project::maximumCountInBars; ++bars)
            menu.addItem(juce::String(bars) + (bars == 1 ? " bar" : " bars"), true,
                         session.getProject().getCountInBars() == bars,
                         [this, bars]
                         {
                             session.setCountInBars(bars);
                             session.setCountInEnabled(true);
                         });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&countInButton));
    };

    recordingStatus.setFont(juce::FontOptions(13.0f));
    recordingStatus.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(recordingStatus);

    playButton.onClick = [this] { session.play(); };
    pauseButton.onClick = [this] { session.pause(); };
    stopButton.onClick = [this] { session.stop(); };
    loopButton.onClick = [this] { session.setLoopEnabled(!session.getProject().isLoopEnabled()); };
    snapButton.onClick = [this] { session.setSnapEnabled(!session.getProject().isSnapEnabled()); };

    styleDisplay(timeLabel, 18.0f);
    styleDisplay(barsLabel, 18.0f);
    timeLabel.setTooltip("Playhead position (minutes:seconds)");
    barsLabel.setTooltip("Playhead position (bar.beat.sixteenth)");
    addAndMakeVisible(timeLabel);
    addAndMakeVisible(barsLabel);

    for (auto* label : {&tempoLabel, &metreLabel})
    {
        styleDisplay(*label, 15.0f);
        label->setEditable(false, true, false);
        addAndMakeVisible(*label);
    }
    tempoLabel.setTooltip("Tempo in quarter notes per minute (double-click to change)");
    metreLabel.setTooltip("Time signature, e.g. 4/4 or 7/8 (double-click to change)");

    tempoLabel.onTextChange = [this] {
        const auto number = tempoLabel.getText().upToFirstOccurrenceOf(" ", false, false);
        const auto value = number.getDoubleValue();
        if (value > 0.0)
            session.setTempo(value);
        showTempoAndMetre();
    };
    metreLabel.onTextChange = [this] {
        const auto text = metreLabel.getText();
        const model::TimeSignature signature{
            text.upToFirstOccurrenceOf("/", false, false).trim().getIntValue(),
            text.fromFirstOccurrenceOf("/", false, false).trim().getIntValue()};
        if (signature.isValid())
            session.setTimeSignature(signature);
        showTempoAndMetre();
    };

    for (int i = 0; i < static_cast<int>(std::size(gridChoices)); ++i)
    {
        const auto division = gridChoices[i];
        const auto name = division == model::GridDivision::automatic ? juce::String("auto")
                                                                     : model::toString(division);
        gridBox.addItem("Grid: " + name, i + 1);
    }
    gridBox.setWantsKeyboardFocus(false);
    gridBox.setTooltip("Grid for snapping; \"auto\" follows the zoom");
    gridBox.onChange = [this] {
        const int index = gridBox.getSelectedItemIndex();
        if (index >= 0)
            session.setGridDivision(gridChoices[index]);
    };
    addAndMakeVisible(gridBox);

    refresh();
}

void TransportBar::refresh()
{
    const auto state = session.getTransportState();
    const bool playing = state == engine::TransportState::playing;
    const bool hasClips = session.getProject().getEnd() > 0;
    const auto playhead = session.getPlayhead();

    const bool recording = session.isRecording();
    playButton.setEnabled(hasClips && !playing && !recording);
    pauseButton.setEnabled(playing);
    stopButton.setEnabled(state != engine::TransportState::stopped || playhead > 0 || recording);
    recordButton.setToggleState(recording, juce::dontSendNotification);
    showRecordingStatus();

    timeLabel.setText(controller::formatTime(playhead), juce::dontSendNotification);
    barsLabel.setText(session.getProject().getTempoMap().formatBarsBeats(playhead),
                      juce::dontSendNotification);

    if (shownRevision != session.getRevision())
    {
        shownRevision = session.getRevision();
        const auto& project = session.getProject();
        loopButton.setToggleState(project.isLoopEnabled(), juce::dontSendNotification);
        snapButton.setToggleState(project.isSnapEnabled(), juce::dontSendNotification);
        metronomeButton.setToggleState(project.isMetronomeEnabled(), juce::dontSendNotification);
        countInButton.setToggleState(project.isCountInEnabled(), juce::dontSendNotification);
        countInButton.setButtonText(project.getCountInBars() == 1 ? "Count" : "Count 2");
        punchButton.setToggleState(project.isPunchEnabled(), juce::dontSendNotification);

        for (int i = 0; i < static_cast<int>(std::size(gridChoices)); ++i)
            if (gridChoices[i] == project.getGridDivision())
                gridBox.setSelectedItemIndex(i, juce::dontSendNotification);

        if (!tempoLabel.isBeingEdited() && !metreLabel.isBeingEdited())
            showTempoAndMetre();
    }
}

void TransportBar::showWarning(const juce::String& text)
{
    warning = text;
    warningUntil = juce::Time::getMillisecondCounter() + 4000;
}

void TransportBar::showRecordingStatus()
{
    using State = controller::RecordingController::State;
    const auto& recording = session.getRecording();
    juce::String text;
    auto colour = juce::Colours::lightgrey;

    const auto minutesLeft = [&recording]
    {
        const auto seconds = recording.getSecondsLeft();
        if (seconds < 0.0)
            return juce::String();
        const auto minutes = static_cast<int>(seconds / 60.0);
        if (minutes >= 120)
            return juce::String(minutes / 60) + " h of disk left";
        if (minutes >= 2)
            return juce::String(minutes) + " min of disk left";
        return juce::String(static_cast<int>(seconds)) + " s of disk left";
    };

    switch (recording.getState())
    {
    case State::countingIn:
        text = "Count-in...";
        colour = juce::Colour(0xffffb74d);
        break;
    case State::recording:
        text = "Recording";
        if (const auto left = minutesLeft(); left.isNotEmpty())
            text << "  -  " << left;
        colour = recording.getSecondsLeft() >= 0.0 &&
                         recording.getSecondsLeft() <
                             controller::RecordingController::lowSpaceWarningSeconds
                     ? juce::Colour(0xffffb74d)
                     : juce::Colour(0xffef5350);
        break;
    case State::finishing:
        text = "Saving the recording...";
        break;
    case State::idle:
        if (juce::Time::getMillisecondCounter() < warningUntil)
        {
            text = warning;
            colour = juce::Colour(0xffffb74d);
        }
        break;
    }

    if (recordingStatus.getText() != text)
        recordingStatus.setText(text, juce::dontSendNotification);
    recordingStatus.setColour(juce::Label::textColourId, colour);
}

void TransportBar::showTempoAndMetre()
{
    const auto tempoMap = session.getProject().getTempoMap();
    const auto signature = tempoMap.getTimeSignature();
    tempoLabel.setText(juce::String(tempoMap.getTempo(), 2) + " BPM", juce::dontSendNotification);
    metreLabel.setText(juce::String(signature.numerator) + "/" +
                           juce::String(signature.denominator),
                       juce::dontSendNotification);
}

void TransportBar::resized()
{
    auto area = getLocalBounds();
    const int gap = 6;

    for (auto* button : {&playButton, &pauseButton, &stopButton, &recordButton, &loopButton})
    {
        button->setBounds(area.removeFromLeft(54));
        area.removeFromLeft(gap);
    }

    area.removeFromLeft(gap);
    timeLabel.setBounds(area.removeFromLeft(110));
    area.removeFromLeft(gap);
    barsLabel.setBounds(area.removeFromLeft(92));
    area.removeFromLeft(2 * gap);
    tempoLabel.setBounds(area.removeFromLeft(92));
    area.removeFromLeft(gap);
    metreLabel.setBounds(area.removeFromLeft(48));

    gridBox.setBounds(area.removeFromRight(110));
    area.removeFromRight(gap);
    snapButton.setBounds(area.removeFromRight(52));
    area.removeFromRight(2 * gap);
    for (auto* button :
         std::initializer_list<juce::Component*>{&punchButton, &countInButton, &metronomeButton})
    {
        button->setBounds(area.removeFromRight(button == &countInButton ? 64 : 52));
        area.removeFromRight(gap);
    }

    area.removeFromLeft(gap);
    recordingStatus.setBounds(area);
}

} // namespace daw::ui::desktop
