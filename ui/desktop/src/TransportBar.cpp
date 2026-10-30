#include "daw/ui/desktop/TransportBar.h"

#include <cmath>

namespace daw::ui::desktop
{

juce::String formatTime(double seconds)
{
    const auto totalMilliseconds = static_cast<juce::int64>(std::floor(std::max(0.0, seconds) * 1000.0));
    const auto minutes = totalMilliseconds / 60000;
    const auto secondsPart = (totalMilliseconds / 1000) % 60;
    const auto milliseconds = totalMilliseconds % 1000;

    return juce::String(minutes) + ":" + juce::String(secondsPart).paddedLeft('0', 2) + "." +
           juce::String(milliseconds).paddedLeft('0', 3);
}

TransportBar::TransportBar()
{
    for (auto* button : {&playButton, &pauseButton, &stopButton, &loopButton})
    {
        // Space and Return are transport shortcuts; a focused button must not eat them.
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }

    playButton.setTooltip("Play (Space)");
    pauseButton.setTooltip("Pause (Space)");
    stopButton.setTooltip("Stop and return to the start (Return)");
    loopButton.setTooltip("Loop the whole file (L)");

    loopButton.setClickingTogglesState(true);
    loopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2e7d32));

    playButton.onClick = [this] {
        if (onPlay)
            onPlay();
    };
    pauseButton.onClick = [this] {
        if (onPause)
            onPause();
    };
    stopButton.onClick = [this] {
        if (onStop)
            onStop();
    };
    loopButton.onClick = [this] {
        if (onLoopToggled)
            onLoopToggled(loopButton.getToggleState());
    };

    timeLabel.setFont(juce::FontOptions(18.0f));
    timeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(timeLabel);

    update(engine::TransportState::stopped, 0, 0, 0.0, false, false);
}

void TransportBar::update(engine::TransportState state, juce::int64 position, juce::int64 length,
                          double sampleRate, bool looping, bool hasAudio)
{
    const bool playing = state == engine::TransportState::playing;

    playButton.setEnabled(hasAudio && !playing);
    pauseButton.setEnabled(hasAudio && playing);
    stopButton.setEnabled(hasAudio && (state != engine::TransportState::stopped || position > 0));
    loopButton.setToggleState(looping, juce::dontSendNotification);

    const auto toSeconds = [sampleRate](juce::int64 samples) {
        return sampleRate > 0.0 ? static_cast<double>(samples) / sampleRate : 0.0;
    };
    timeLabel.setText(formatTime(toSeconds(position)) + "  /  " + formatTime(toSeconds(length)),
                      juce::dontSendNotification);
}

void TransportBar::resized()
{
    auto area = getLocalBounds();
    const int gap = 6;

    for (auto* button : {&playButton, &pauseButton, &stopButton, &loopButton})
    {
        button->setBounds(area.removeFromLeft(72));
        area.removeFromLeft(gap);
    }

    area.removeFromLeft(2 * gap);
    timeLabel.setBounds(area);
}

} // namespace daw::ui::desktop
