#pragma once

#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

// What each built-in theme draws beyond its data. Made by ThemedLookAndFeel::make().

/** Neon Grid: a glowing neon logo that flickers, scanlines scrolling over the
    timeline, a pulsing playhead, neon gradients on progress. */
class NeonGridLookAndFeel final : public ThemedLookAndFeel
{
public:
    using ThemedLookAndFeel::ThemedLookAndFeel;

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String& textToShow) override;
    void drawLogo(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                  double seconds, bool glitching) const override;
    [[nodiscard]] float logoWidth(const juce::String& text, float height) const override;
    [[nodiscard]] bool hasTimelineBackground() const override { return true; }
    void drawTimelineBackground(juce::Graphics& g, juce::Rectangle<float> area,
                                double seconds) const override;
    [[nodiscard]] bool playheadAnimates(bool playing) const override;
    void drawPlayhead(juce::Graphics& g, float x, float top, float bottom,
                      const PlayheadLook& look) const override;
};

/** Acid Terminal: terminal-style headings ("[ MIXER ]"), progress as a row of blocks, a
    banner logo that glitches, hazard stripes under the transport. */
class AcidTerminalLookAndFeel final : public ThemedLookAndFeel
{
public:
    using ThemedLookAndFeel::ThemedLookAndFeel;

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String& textToShow) override;
    void drawLogo(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                  double seconds, bool glitching) const override;
    [[nodiscard]] float logoWidth(const juce::String& text, float height) const override;
    [[nodiscard]] int hazardBandHeight() const override { return 6; }
    void drawHazardBand(juce::Graphics& g, juce::Rectangle<float> area, double seconds,
                        bool recording) const override;

protected:
    [[nodiscard]] juce::String labelText(appearance::FontRole role,
                                         const juce::String& text) const override;
};

/** Synth Chrome: chrome gradients on buttons and the logo (with a reflection sweeping
    across it), a sunset gradient on progress, a perspective grid scrolling under the
    timeline, a trail behind the playhead. */
class SynthChromeLookAndFeel final : public ThemedLookAndFeel
{
public:
    using ThemedLookAndFeel::ThemedLookAndFeel;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour, bool isMouseOverButton,
                              bool isButtonDown) override;
    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String& textToShow) override;
    void drawLogo(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                  double seconds, bool glitching) const override;
    [[nodiscard]] float logoWidth(const juce::String& text, float height) const override;
    [[nodiscard]] bool hasTimelineBackground() const override { return true; }
    [[nodiscard]] bool timelineBackgroundIsUnder() const override { return true; }
    void drawTimelineBackground(juce::Graphics& g, juce::Rectangle<float> area,
                                double seconds) const override;
    [[nodiscard]] bool playheadAnimates(bool playing) const override { return playing; }
    void drawPlayhead(juce::Graphics& g, float x, float top, float bottom,
                      const PlayheadLook& look) const override;

protected:
    [[nodiscard]] int playheadTrail() const noexcept override { return 56; }
};

} // namespace daw::ui::desktop
