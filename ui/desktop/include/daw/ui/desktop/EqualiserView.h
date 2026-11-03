#pragma once

#include "daw/controller/Session.h"
#include "daw/engine/fx/Equaliser.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace daw::ui::desktop
{

/**
    The equaliser's response curve (the exact response of its settings, from
    Equaliser::magnitude()), 20 Hz to 20 kHz and -24 to +24 dB, with a handle per
    band: drag it for frequency and gain (one undo step), the wheel changes its Q,
    double-click switches the band on or off. Clicking a handle selects its band.
    Automated parameters follow their automation and cannot be changed here.
*/
class EqualiserView final : public juce::Component
{
public:
    EqualiserView(controller::Session& sessionToEdit, model::ObjectId effect);

    std::function<void(int band)> onBandSelected;

    /** Rereads the values (and their automation); repaints if they changed. */
    void refresh();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    [[nodiscard]] juce::Rectangle<float> plot() const;
    [[nodiscard]] float xOf(double frequency) const;
    [[nodiscard]] float yOf(double decibels) const;
    [[nodiscard]] double frequencyAt(float x) const;
    [[nodiscard]] double decibelsAt(float y) const;
    [[nodiscard]] juce::Point<float> handleOf(int band) const;
    [[nodiscard]] int bandAt(juce::Point<float> point) const;
    [[nodiscard]] float valueOf(int index) const;
    void set(int index, float value);

    controller::Session& session;
    const model::ObjectId effectId;
    engine::fx::Equaliser reference; // for its description
    std::vector<float> values;
    double sampleRate = 48000.0;
    int selected = 2;
    int dragging = -1;
};

} // namespace daw::ui::desktop
