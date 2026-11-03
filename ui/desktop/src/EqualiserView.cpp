#include "daw/ui/desktop/EqualiserView.h"

#include <cmath>

namespace daw::ui::desktop
{

namespace
{
constexpr double lowest = 20.0;
constexpr double highest = 20000.0;
constexpr double range = 24.0; // dB above and below 0

const std::array<juce::Colour, engine::fx::Equaliser::numBands> bandColours{
    juce::Colour(0xff9e9e9e), juce::Colour(0xffef5350), juce::Colour(0xffffa726),
    juce::Colour(0xffffee58), juce::Colour(0xff66bb6a), juce::Colour(0xff26c6da),
    juce::Colour(0xff5c6bc0), juce::Colour(0xff9e9e9e)};
} // namespace

EqualiserView::EqualiserView(controller::Session& sessionToEdit, model::ObjectId effect)
    : session(sessionToEdit)
    , effectId(effect)
{
    refresh();
}

float EqualiserView::valueOf(int index) const
{
    return index >= 0 && static_cast<std::size_t>(index) < values.size()
               ? values[static_cast<std::size_t>(index)]
               : 0.0f;
}

void EqualiserView::refresh()
{
    const auto location = session.findEffect(effectId);
    const auto effect = session.getEffect(effectId);
    if (!location || !effect)
        return;

    std::vector<float> now;
    for (const auto& info : reference.getDescription().parameters)
    {
        float value = info.clamp(static_cast<float>(
            effect->getParameter(info.id).value_or(static_cast<double>(info.defaultValue))));
        if (const auto automated = session.getAutomatedValue(
                location->owner, model::AutomationTarget::effectParameter(effectId, info.id)))
            value = info.fromNormalised(static_cast<float>(*automated));
        now.push_back(value);
    }

    sampleRate = session.getSampleRate();
    if (now != values)
    {
        values = std::move(now);
        repaint();
    }
}

juce::Rectangle<float> EqualiserView::plot() const
{
    return getLocalBounds().toFloat().reduced(28.0f, 12.0f).withTrimmedBottom(10.0f);
}

float EqualiserView::xOf(double frequency) const
{
    const auto area = plot();
    const double position = std::log(frequency / lowest) / std::log(highest / lowest);
    return area.getX() + static_cast<float>(position) * area.getWidth();
}

float EqualiserView::yOf(double decibels) const
{
    const auto area = plot();
    return area.getCentreY() - static_cast<float>(decibels / range) * area.getHeight() * 0.5f;
}

double EqualiserView::frequencyAt(float x) const
{
    const auto area = plot();
    const double position = juce::jlimit(0.0, 1.0, static_cast<double>((x - area.getX()) / area.getWidth()));
    return lowest * std::pow(highest / lowest, position);
}

double EqualiserView::decibelsAt(float y) const
{
    const auto area = plot();
    return juce::jlimit(-range, range,
                        static_cast<double>((area.getCentreY() - y) / (area.getHeight() * 0.5f)) * range);
}

juce::Point<float> EqualiserView::handleOf(int band) const
{
    const auto parameters = engine::fx::Equaliser::parametersOf(band);
    const double gain = parameters.gain >= 0 ? valueOf(parameters.gain) : 0.0;
    return {xOf(juce::jlimit(lowest, highest, static_cast<double>(valueOf(parameters.frequency)))),
            yOf(gain)};
}

int EqualiserView::bandAt(juce::Point<float> point) const
{
    for (int band = engine::fx::Equaliser::numBands - 1; band >= 0; --band)
        if (handleOf(band).getDistanceFrom(point) <= 9.0f)
            return band;
    return -1;
}

void EqualiserView::paint(juce::Graphics& g)
{
    const auto area = plot();
    g.fillAll(juce::Colour(0xff161a1e));

    // Grid: decades and their halves, every 6 dB.
    g.setFont(juce::FontOptions(10.0f));
    for (const double frequency : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const float x = xOf(frequency);
        g.setColour(juce::Colour(0xff2a3138));
        g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());
        g.setColour(juce::Colour(0xff6f7a84));
        g.drawText(frequency >= 1000.0 ? juce::String(frequency / 1000.0) + "k" : juce::String(frequency),
                   juce::Rectangle<float>(x - 20.0f, area.getBottom() + 1.0f, 40.0f, 12.0f),
                   juce::Justification::centred);
    }
    for (double decibels = -range; decibels <= range; decibels += 6.0)
    {
        const float y = yOf(decibels);
        g.setColour(juce::exactlyEqual(decibels, 0.0) ? juce::Colour(0xff46505a) : juce::Colour(0xff2a3138));
        g.drawHorizontalLine(static_cast<int>(y), area.getX(), area.getRight());
        g.setColour(juce::Colour(0xff6f7a84));
        g.drawText(juce::String(static_cast<int>(decibels)),
                   juce::Rectangle<float>(0.0f, y - 6.0f, area.getX() - 4.0f, 12.0f),
                   juce::Justification::centredRight);
    }

    if (values.empty())
        return;

    const auto curve = [&](auto&& magnitudeAt) {
        juce::Path path;
        const int points = std::max(2, static_cast<int>(area.getWidth()));
        for (int i = 0; i < points; ++i)
        {
            const float x = area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(points - 1);
            const double decibels = 20.0 * std::log10(std::max(1.0e-6, magnitudeAt(frequencyAt(x))));
            const float y = juce::jlimit(area.getY(), area.getBottom(), yOf(decibels));
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    };

    // The selected band alone, faint; then the whole response.
    g.setColour(bandColours[static_cast<std::size_t>(selected)].withAlpha(0.35f));
    g.strokePath(curve([&](double f) {
                     return engine::fx::Equaliser::bandMagnitude(values, selected, f, sampleRate);
                 }),
                 juce::PathStrokeType(1.5f));
    g.setColour(juce::Colours::white);
    g.strokePath(curve([&](double f) { return engine::fx::Equaliser::magnitude(values, f, sampleRate); }),
                 juce::PathStrokeType(2.0f));

    for (int band = 0; band < engine::fx::Equaliser::numBands; ++band)
    {
        const auto parameters = engine::fx::Equaliser::parametersOf(band);
        const bool on = valueOf(parameters.on) >= 0.5f;
        const auto centre = handleOf(band);
        const auto colour = bandColours[static_cast<std::size_t>(band)];
        g.setColour(on ? colour : colour.withAlpha(0.3f));
        g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(centre));
        if (band == selected)
        {
            g.setColour(juce::Colours::white);
            g.drawEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(centre), 1.5f);
        }
        g.setColour(juce::Colours::black);
        g.drawText(juce::String(band + 1), juce::Rectangle<float>(14.0f, 14.0f).withCentre(centre),
                   juce::Justification::centred);
    }
}

void EqualiserView::set(int index, float value)
{
    if (index >= 0)
        session.setEffectParameter(effectId,
                                   reference.getDescription().parameters[static_cast<std::size_t>(index)].id,
                                   value);
}

void EqualiserView::mouseDown(const juce::MouseEvent& event)
{
    const int band = bandAt(event.position);
    if (band < 0)
        return;

    selected = band;
    if (onBandSelected)
        onBandSelected(band);
    dragging = band;
    session.beginGesture("Equaliser Band");
    repaint();
}

void EqualiserView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging < 0)
        return;

    const auto parameters = engine::fx::Equaliser::parametersOf(dragging);
    set(parameters.frequency, static_cast<float>(frequencyAt(event.position.x)));
    if (parameters.gain >= 0)
        set(parameters.gain, static_cast<float>(decibelsAt(event.position.y)));
}

void EqualiserView::mouseUp(const juce::MouseEvent&)
{
    if (dragging >= 0)
        session.endGesture();
    dragging = -1;
}

void EqualiserView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const int band = bandAt(event.position);
    if (band < 0)
        return;
    const auto on = engine::fx::Equaliser::parametersOf(band).on;
    set(on, valueOf(on) >= 0.5f ? 0.0f : 1.0f);
}

void EqualiserView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const int band = bandAt(event.position);
    const auto parameters = engine::fx::Equaliser::parametersOf(band >= 0 ? band : selected);
    if (parameters.q < 0)
        return;
    const float factor = wheel.deltaY > 0.0f ? 1.15f : 1.0f / 1.15f;
    set(parameters.q, juce::jlimit(0.1f, 18.0f, valueOf(parameters.q) * factor));
}

} // namespace daw::ui::desktop
