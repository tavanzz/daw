#pragma once

#include "daw/controller/Session.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>

namespace daw::ui::desktop
{

/**
    Audio settings: the devices and their channels (JUCE's selector, inputs
    included), and recording: the file format, the latency the device reports, the
    manual offset, and a button to measure it with a cable. Opening it asks for the
    inputs (and so for the microphone permission, the first time).
*/
class AudioSettingsDialog final : public juce::Component, private juce::Timer
{
public:
    AudioSettingsDialog(controller::Session& sessionToEdit, juce::AudioDeviceManager& devices);
    ~AudioSettingsDialog() override;

    /** Shows the dialog in its own window, centred on parent. */
    static void show(controller::Session& session, juce::AudioDeviceManager& devices,
                     juce::Component* parent);

    void resized() override;

private:
    void timerCallback() override;
    void showLatency();

    controller::Session& session;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label recordingHeading{{}, "Recording"};
    juce::Label formatLabel{{}, "File format"};
    juce::ComboBox formatBox;
    juce::Label reportedLabel;
    juce::Label offsetLabel{{}, "Latency offset"};
    juce::Slider offsetSlider;
    juce::Label totalLabel;
    juce::TextButton measureButton{"Measure with a Cable..."};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsDialog)
};

/**
    Measures the interface's round trip with a cable from an output to an input, and
    offers to use the result as the latency offset.
*/
class LatencyMeasurementDialog final : public juce::Component, private juce::Timer
{
public:
    explicit LatencyMeasurementDialog(controller::Session& sessionToEdit);
    ~LatencyMeasurementDialog() override;

    static void show(controller::Session& session, juce::Component* parent);

    void resized() override;

private:
    void timerCallback() override;
    void startMeasuring();

    controller::Session& session;
    juce::Label instructions;
    juce::Label outputLabel{{}, "Output"};
    juce::Label inputLabel{{}, "Input"};
    juce::ComboBox outputBox;
    juce::ComboBox inputBox;
    juce::TextButton measureButton{"Measure"};
    double progress = 0.0;
    juce::ProgressBar progressBar{progress};
    juce::Label resultLabel;
    juce::TextButton applyButton{"Use This Offset"};
    std::optional<controller::LatencyMeasurement::Result> result;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LatencyMeasurementDialog)
};

} // namespace daw::ui::desktop
