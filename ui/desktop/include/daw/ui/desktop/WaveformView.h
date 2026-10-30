#pragma once

#include "daw/engine/dsp/WaveformPeaks.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui::desktop
{

/** The loaded file's waveform (one lane per channel, up to two) with the playhead.
    Clicking or dragging asks to move the playhead there. Without audio it shows a
    message instead (how to load a file, or that one is loading). */
class WaveformView final : public juce::Component
{
public:
    WaveformView();

    /** A position in samples, as asked for by a click or drag. */
    std::function<void(juce::int64)> onSeek;

    void setPeaks(std::shared_ptr<const engine::dsp::WaveformPeaks> newPeaks);
    void showMessage(const juce::String& text);
    void setPlayhead(juce::int64 position);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    void renderWaveform();
    [[nodiscard]] float xForPosition(juce::int64 position) const;
    [[nodiscard]] juce::int64 positionForX(float x) const;
    void seekTo(float x);

    std::shared_ptr<const engine::dsp::WaveformPeaks> peaks;
    juce::String message;
    juce::Image waveformImage; // the waveform only; the playhead is drawn on top
    juce::int64 playhead = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};

} // namespace daw::ui::desktop
