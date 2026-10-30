#include "daw/ui/desktop/WaveformView.h"

#include <algorithm>
#include <cmath>

namespace daw::ui::desktop
{

namespace
{
const juce::Colour backgroundColour{0xff15181c};
const juce::Colour laneSeparatorColour{0xff2a2f36};
const juce::Colour waveformColour{0xff4fc3f7};
const juce::Colour playheadColour{0xffffb74d};
} // namespace

WaveformView::WaveformView()
{
    setOpaque(true);
}

void WaveformView::setPeaks(std::shared_ptr<const engine::dsp::WaveformPeaks> newPeaks)
{
    peaks = std::move(newPeaks);
    message.clear();
    playhead = 0;
    renderWaveform();
    repaint();
}

void WaveformView::showMessage(const juce::String& text)
{
    peaks.reset();
    message = text;
    waveformImage = {};
    repaint();
}

void WaveformView::setPlayhead(juce::int64 position)
{
    if (position == playhead)
        return;

    // Only the two columns around the old and new playhead need redrawing.
    const auto oldX = static_cast<int>(xForPosition(playhead));
    playhead = position;
    const auto newX = static_cast<int>(xForPosition(playhead));

    repaint(oldX - 2, 0, 5, getHeight());
    repaint(newX - 2, 0, 5, getHeight());
}

void WaveformView::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    if (peaks == nullptr)
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(15.0f));
        g.drawFittedText(message, getLocalBounds().reduced(20), juce::Justification::centred, 3);
        return;
    }

    g.drawImageAt(waveformImage, 0, 0);

    g.setColour(playheadColour);
    g.fillRect(juce::Rectangle<float>(xForPosition(playhead) - 0.5f, 0.0f, 1.5f,
                                      static_cast<float>(getHeight())));
}

void WaveformView::resized()
{
    renderWaveform();
}

void WaveformView::mouseDown(const juce::MouseEvent& event)
{
    seekTo(event.position.x);
}

void WaveformView::mouseDrag(const juce::MouseEvent& event)
{
    seekTo(event.position.x);
}

void WaveformView::renderWaveform()
{
    if (peaks == nullptr || getWidth() <= 0 || getHeight() <= 0)
    {
        waveformImage = {};
        return;
    }

    waveformImage = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    juce::Graphics g(waveformImage);
    g.fillAll(backgroundColour);

    const int lanes = std::clamp(peaks->getNumChannels(), 1, 2);
    const float laneHeight = static_cast<float>(getHeight()) / static_cast<float>(lanes);
    const auto length = peaks->getNumSamples();

    for (int lane = 0; lane < lanes; ++lane)
    {
        const float top = laneHeight * static_cast<float>(lane);
        const float centre = top + laneHeight / 2.0f;
        const float halfHeight = laneHeight / 2.0f - 2.0f;

        if (lane > 0)
        {
            g.setColour(laneSeparatorColour);
            g.fillRect(0.0f, top, static_cast<float>(getWidth()), 1.0f);
        }

        g.setColour(waveformColour);
        for (int x = 0; x < getWidth(); ++x)
        {
            const auto start = length * x / getWidth();
            const auto end = std::max(start + 1, length * (x + 1) / getWidth());
            const auto range = peaks->getRange(lane, start, end);

            const float yTop = centre - std::clamp(range.max, -1.0f, 1.0f) * halfHeight;
            const float yBottom = centre - std::clamp(range.min, -1.0f, 1.0f) * halfHeight;
            g.fillRect(static_cast<float>(x), yTop, 1.0f, std::max(1.0f, yBottom - yTop));
        }
    }
}

float WaveformView::xForPosition(juce::int64 position) const
{
    if (peaks == nullptr || peaks->getNumSamples() <= 0)
        return 0.0f;

    return static_cast<float>(static_cast<double>(position) * getWidth() /
                              static_cast<double>(peaks->getNumSamples()));
}

juce::int64 WaveformView::positionForX(float x) const
{
    if (peaks == nullptr || getWidth() <= 0)
        return 0;

    const double proportion = std::clamp(static_cast<double>(x) / getWidth(), 0.0, 1.0);
    return static_cast<juce::int64>(std::llround(proportion * static_cast<double>(peaks->getNumSamples())));
}

void WaveformView::seekTo(float x)
{
    if (peaks != nullptr && onSeek)
        onSeek(positionForX(x));
}

} // namespace daw::ui::desktop
