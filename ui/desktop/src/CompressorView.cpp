#include "daw/ui/desktop/CompressorView.h"

#include "daw/engine/fx/Compressor.h"
#include "daw/engine/fx/Limiter.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace daw::ui::desktop
{

namespace
{
constexpr float lowestDb = -60.0f;
constexpr float mostReduction = 24.0f;
} // namespace

CompressorView::CompressorView(controller::Session& sessionToEdit, model::ObjectId effect)
    : session(sessionToEdit)
    , effectId(effect)
{
    const auto model = session.getEffect(effectId);
    isLimiter = model && model->getType() == engine::fx::Limiter::typeId;
}

void CompressorView::refresh(double elapsedSeconds)
{
    float taken = 0.0f;
    if (const auto instance = session.getEffectInstance(effectId))
    {
        if (auto* compressor = dynamic_cast<engine::fx::Compressor*>(&instance->getEffect()))
        {
            taken = compressor->getGainReduction().take();
            inputLevel = compressor->takeInputLevel();
        }
        else if (auto* limiter = dynamic_cast<engine::fx::Limiter*>(&instance->getEffect()))
        {
            taken = limiter->getGainReduction().take();
        }
    }

    // The bar jumps up and falls back at 20 dB per second, as a meter would.
    reduction = std::max(taken, reduction - static_cast<float>(20.0 * elapsedSeconds));
    history[static_cast<std::size_t>(historyPosition)] = taken;
    historyPosition = (historyPosition + 1) % historyLength;

    if (const auto effect = session.getEffect(effectId); effect && !isLimiter)
    {
        threshold = static_cast<float>(effect->getParameter("threshold").value_or(-18.0));
        ratio = static_cast<float>(effect->getParameter("ratio").value_or(4.0));
        knee = static_cast<float>(effect->getParameter("knee").value_or(6.0));
    }
    repaint();
}

void CompressorView::paintCurve(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& look = ThemedLookAndFeel::get(*this);
    look.drawGraphBackground(g, area);
    const auto x = [&](float db) { return area.getX() + (db - lowestDb) / -lowestDb * area.getWidth(); };
    const auto y = [&](float db) { return area.getBottom() - (db - lowestDb) / -lowestDb * area.getHeight(); };

    for (float db = lowestDb; db <= 0.0f; db += 12.0f)
    {
        look.drawGraphLine(g, {x(db), area.getY(), x(db), area.getBottom()}, false);
        look.drawGraphLine(g, {area.getX(), y(db), area.getRight(), y(db)}, false);
    }
    look.drawGraphLine(g, {x(lowestDb), y(lowestDb), x(0.0f), y(0.0f)}, true); // unity

    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const float in = lowestDb + -lowestDb * static_cast<float>(i) / 120.0f;
        const auto out = static_cast<float>(engine::fx::Compressor::staticCurve(in, threshold, ratio, knee));
        if (i == 0)
            curve.startNewSubPath(x(in), y(out));
        else
            curve.lineTo(x(in), y(std::max(lowestDb, out)));
    }
    look.drawCurve(g, curve, area.getBottom(), false);

    if (inputLevel > lowestDb)
    {
        const auto out = static_cast<float>(engine::fx::Compressor::staticCurve(inputLevel, threshold, ratio, knee));
        look.drawLevelDot(g, {x(std::min(0.0f, inputLevel)), y(std::min(0.0f, out))});
    }
}

void CompressorView::paint(juce::Graphics& g)
{
    const auto& look = ThemedLookAndFeel::get(*this);
    look.drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::panel, false);
    auto area = getLocalBounds().toFloat().reduced(8.0f);

    if (!isLimiter)
    {
        paintCurve(g, area.removeFromLeft(area.getHeight()));
        area.removeFromLeft(8.0f);
    }

    // The bar: reduction from the top.
    auto bar = area.removeFromRight(16.0f);
    look.drawGainReductionBar(g, bar, reduction / mostReduction);
    area.removeFromRight(8.0f);

    // The history, newest on the right.
    look.drawGraphBackground(g, area);
    juce::Path trace;
    for (int i = 0; i < historyLength; ++i)
    {
        const float value = history[static_cast<std::size_t>((historyPosition + i) % historyLength)];
        const float px = area.getX() + area.getWidth() * static_cast<float>(i) / (historyLength - 1);
        const float py = area.getY() + area.getHeight() * std::min(1.0f, value / mostReduction);
        if (i == 0)
            trace.startNewSubPath(px, py);
        else
            trace.lineTo(px, py);
    }
    look.drawGainReductionTrace(g, trace);
    look.drawGraphLabel(g, "Gain reduction " + juce::String(reduction, 1) + " dB",
                               area.reduced(4.0f), juce::Justification::bottomLeft);
}

} // namespace daw::ui::desktop
