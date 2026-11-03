#pragma once

#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

// What each built-in theme draws beyond its data. Made by ThemedLookAndFeel::make().

/** Neon Grid: a glowing neon logo, neon gradients on progress. */
class NeonGridLookAndFeel final : public ThemedLookAndFeel
{
public:
    using ThemedLookAndFeel::ThemedLookAndFeel;

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String& textToShow) override;
    void drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                  const juce::String& text) const override;
};

/** Acid Terminal: terminal-style headings ("[ MIXER ]"), progress as a row of blocks. */
class AcidTerminalLookAndFeel final : public ThemedLookAndFeel
{
public:
    using ThemedLookAndFeel::ThemedLookAndFeel;

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String& textToShow) override;
    void drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                  const juce::String& text) const override;

protected:
    [[nodiscard]] juce::String labelText(appearance::FontRole role,
                                         const juce::String& text) const override;
};

/** Synth Chrome: chrome gradients on buttons and the logo, a sunset gradient on
    progress. */
class SynthChromeLookAndFeel final : public ThemedLookAndFeel
{
public:
    using ThemedLookAndFeel::ThemedLookAndFeel;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour, bool isMouseOverButton,
                              bool isButtonDown) override;
    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String& textToShow) override;
    void drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                  const juce::String& text) const override;
};

} // namespace daw::ui::desktop
