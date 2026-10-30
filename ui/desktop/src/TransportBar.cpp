#include "daw/ui/desktop/TransportBar.h"

#include "daw/controller/TimelineViewport.h"

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
    for (auto* button : {&playButton, &pauseButton, &stopButton, &loopButton, &snapButton})
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

    for (auto* button : {&loopButton, &snapButton})
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2e7d32));

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

    playButton.setEnabled(hasClips && !playing);
    pauseButton.setEnabled(playing);
    stopButton.setEnabled(state != engine::TransportState::stopped || playhead > 0);

    timeLabel.setText(controller::formatTime(playhead), juce::dontSendNotification);
    barsLabel.setText(session.getProject().getTempoMap().formatBarsBeats(playhead),
                      juce::dontSendNotification);

    if (shownRevision != session.getRevision())
    {
        shownRevision = session.getRevision();
        const auto& project = session.getProject();
        loopButton.setToggleState(project.isLoopEnabled(), juce::dontSendNotification);
        snapButton.setToggleState(project.isSnapEnabled(), juce::dontSendNotification);

        for (int i = 0; i < static_cast<int>(std::size(gridChoices)); ++i)
            if (gridChoices[i] == project.getGridDivision())
                gridBox.setSelectedItemIndex(i, juce::dontSendNotification);

        if (!tempoLabel.isBeingEdited() && !metreLabel.isBeingEdited())
            showTempoAndMetre();
    }
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

    for (auto* button : {&playButton, &pauseButton, &stopButton, &loopButton})
    {
        button->setBounds(area.removeFromLeft(64));
        area.removeFromLeft(gap);
    }

    area.removeFromLeft(gap);
    timeLabel.setBounds(area.removeFromLeft(120));
    area.removeFromLeft(gap);
    barsLabel.setBounds(area.removeFromLeft(100));
    area.removeFromLeft(2 * gap);
    tempoLabel.setBounds(area.removeFromLeft(96));
    area.removeFromLeft(gap);
    metreLabel.setBounds(area.removeFromLeft(52));

    gridBox.setBounds(area.removeFromRight(120));
    area.removeFromRight(gap);
    snapButton.setBounds(area.removeFromRight(56));
}

} // namespace daw::ui::desktop
