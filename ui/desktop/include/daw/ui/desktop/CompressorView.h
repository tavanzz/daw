#pragma once

#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace daw::ui::desktop
{

/**
    What a compressor (or a limiter) is doing: for a compressor its static curve
    (input level against output level, from Compressor::staticCurve(), with the input
    level now as a dot), and for both the gain reduction as a bar and as a history of
    the last few seconds.
*/
class CompressorView final : public juce::Component
{
public:
    CompressorView(controller::Session& sessionToEdit, model::ObjectId effect);

    /** Reads the effect's meters and values; call at the UI's refresh rate. */
    void refresh(double elapsedSeconds);

    void paint(juce::Graphics& g) override;

private:
    static constexpr int historyLength = 240; // 4 s at 60 Hz

    void paintCurve(juce::Graphics& g, juce::Rectangle<float> area) const;

    controller::Session& session;
    const model::ObjectId effectId;
    bool isLimiter = false;
    float threshold = -18.0f;
    float ratio = 4.0f;
    float knee = 6.0f;
    float inputLevel = -200.0f;
    float reduction = 0.0f; // with ballistics: falls back slowly
    std::array<float, historyLength> history{};
    int historyPosition = 0;
};

} // namespace daw::ui::desktop
