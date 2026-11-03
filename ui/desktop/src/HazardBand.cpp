#include "daw/ui/desktop/HazardBand.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

HazardBand::HazardBand()
{
    setInterceptsMouseClicks(false, false);
    lookAndFeelChanged();
}

void HazardBand::setRecording(bool isRecording)
{
    if (recording == isRecording)
        return;

    recording = isRecording;
    repaint();
}

void HazardBand::lookAndFeelChanged()
{
    stripes.setWantsFrames(ThemedLookAndFeel::get(*this).hazardBandHeight() > 0);
    repaint();
}

void HazardBand::paint(juce::Graphics& g)
{
    // Still stripes when the effect is off.
    ThemedLookAndFeel::get(*this).drawHazardBand(
        g, getLocalBounds().toFloat(), stripes.isActive() ? stripes.getTime() : 0.0, recording);
}

ThemeCrossfade::ThemeCrossfade(juce::Image oldLook, std::function<void()> onFinished)
    : image(std::move(oldLook)), finished(std::move(onFinished))
{
    setInterceptsMouseClicks(false, false);
}

void ThemeCrossfade::paint(juce::Graphics& g)
{
    g.setOpacity(opacity);
    g.drawImage(image, getLocalBounds().toFloat());
}

void ThemeCrossfade::advance(double now)
{
    if (start < 0.0)
        start = now;

    const auto progress = static_cast<float>((now - start) / seconds);
    opacity = 1.0f - std::min(1.0f, progress);
    repaint();

    if (progress >= 1.0f || !fade.isActive())
    {
        fade.setWantsFrames(false);
        // The owner deletes this: not from inside the clock's call.
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<ThemeCrossfade>(this)]
            {
                if (safe != nullptr && safe->finished)
                    safe->finished();
            });
    }
}

} // namespace daw::ui::desktop
