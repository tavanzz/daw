#include "daw/ui/desktop/AudioSettingsDialog.h"

namespace daw::ui::desktop
{

namespace
{

constexpr int formatChoices[] = {16, 24, 32};

void launch(std::unique_ptr<juce::Component> content, const juce::String& title,
            juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle = title;
    options.dialogBackgroundColour =
        parent != nullptr
            ? parent->getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId)
            : juce::Colours::darkgrey;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = parent;
    options.launchAsync();
}

juce::String milliseconds(juce::int64 samples, double sampleRate)
{
    return sampleRate > 0.0 ? juce::String(static_cast<double>(samples) * 1000.0 / sampleRate, 1) +
                                  " ms"
                            : juce::String("?");
}

} // namespace

// AudioSettingsDialog ----------------------------------------------------------------

AudioSettingsDialog::AudioSettingsDialog(controller::Session& sessionToEdit,
                                         juce::AudioDeviceManager& devices)
    : session(sessionToEdit)
    , selector(devices, 0, engine::AudioInputs::maxChannels, 1, 2, false, false, false, false)
{
    // Choosing inputs needs them open: the first time, this asks for the microphone.
    session.requestInputs();

    addAndMakeVisible(selector);

    recordingHeading.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    addAndMakeVisible(recordingHeading);

    formatBox.addItem("WAV 16-bit", 1);
    formatBox.addItem("WAV 24-bit", 2);
    formatBox.addItem("WAV 32-bit float", 3);
    for (int i = 0; i < 3; ++i)
        if (formatChoices[i] == session.getProject().getRecordingBitsPerSample())
            formatBox.setSelectedItemIndex(i, juce::dontSendNotification);
    formatBox.onChange = [this] {
        const int index = formatBox.getSelectedItemIndex();
        if (index >= 0)
            session.setRecordingBitsPerSample(formatChoices[index]);
    };
    formatBox.setTooltip("How new recordings are written (at the device's sample rate)");

    offsetSlider.setSliderStyle(juce::Slider::IncDecButtons);
    offsetSlider.setRange(-20000.0, 20000.0, 1.0);
    offsetSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 80, 22);
    offsetSlider.setTextValueSuffix(" samples");
    offsetSlider.setValue(session.getLatencyOffset(), juce::dontSendNotification);
    offsetSlider.onValueChange = [this] {
        session.setLatencyOffset(static_cast<int>(offsetSlider.getValue()));
        showLatency();
    };
    offsetSlider.setTooltip("Added to the latency the device reports. Measure it with a cable, "
                            "or adjust it until recordings line up.");

    measureButton.onClick = [this] { LatencyMeasurementDialog::show(session, this); };

    for (auto* label : {&formatLabel, &offsetLabel})
        label->setJustificationType(juce::Justification::centredRight);
    for (auto* label : {&reportedLabel, &totalLabel})
        label->setFont(juce::FontOptions(13.0f));

    for (auto* component : std::initializer_list<juce::Component*>{
             &formatLabel, &formatBox, &reportedLabel, &offsetLabel, &offsetSlider, &totalLabel,
             &measureButton})
        addAndMakeVisible(*component);

    showLatency();
    setSize(560, 700);
    startTimerHz(4);
}

AudioSettingsDialog::~AudioSettingsDialog()
{
    stopTimer();
}

void AudioSettingsDialog::show(controller::Session& session, juce::AudioDeviceManager& devices,
                               juce::Component* parent)
{
    launch(std::make_unique<AudioSettingsDialog>(session, devices), "Audio Settings", parent);
}

void AudioSettingsDialog::timerCallback()
{
    // The device may change while the dialog is open (here, or unplugged).
    if (!offsetSlider.isMouseButtonDown())
        offsetSlider.setValue(session.getLatencyOffset(), juce::dontSendNotification);
    showLatency();
}

void AudioSettingsDialog::showLatency()
{
    const double rate = session.getEngine().getSampleRate();
    const auto input = session.getDeviceInputLatency();
    const auto output = session.getDeviceOutputLatency();
    reportedLabel.setText("The device reports " + juce::String(input) + " samples in and " +
                              juce::String(output) + " out (" + milliseconds(input + output, rate) +
                              " round trip).",
                          juce::dontSendNotification);
    totalLabel.setText("Recordings are moved earlier by " +
                           juce::String(session.getRecordingLatency()) + " samples (" +
                           milliseconds(session.getRecordingLatency(), rate) + ").",
                       juce::dontSendNotification);
}

void AudioSettingsDialog::resized()
{
    auto area = getLocalBounds().reduced(12);
    selector.setBounds(area.removeFromTop(480));
    area.removeFromTop(8);
    recordingHeading.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);

    const auto row = [&area](juce::Label& label, juce::Component& control, int width) {
        auto line = area.removeFromTop(26);
        label.setBounds(line.removeFromLeft(130));
        line.removeFromLeft(8);
        control.setBounds(line.removeFromLeft(width));
        area.removeFromTop(6);
    };
    row(formatLabel, formatBox, 200);
    reportedLabel.setBounds(area.removeFromTop(22).withTrimmedLeft(138));
    row(offsetLabel, offsetSlider, 200);
    totalLabel.setBounds(area.removeFromTop(22).withTrimmedLeft(138));
    area.removeFromTop(8);
    measureButton.setBounds(area.removeFromTop(28).withTrimmedLeft(138).withWidth(200));
}

// LatencyMeasurementDialog ------------------------------------------------------------

LatencyMeasurementDialog::LatencyMeasurementDialog(controller::Session& sessionToEdit)
    : session(sessionToEdit)
{
    session.requestInputs();

    instructions.setText(
        "Connect a cable from the output below to the input below on your audio interface, "
        "with nothing else attached to them (no microphone). Turn your speakers and "
        "headphones down: the test plays short bursts of noise, about three seconds in all.",
        juce::dontSendNotification);
    instructions.setJustificationType(juce::Justification::topLeft);
    instructions.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(instructions);

    outputBox.addItem("Out 1 (left)", 1);
    outputBox.addItem("Out 2 (right)", 2);
    outputBox.setSelectedId(1, juce::dontSendNotification);

    const auto& names = session.getInputChannelNames();
    const int inputs = names.isEmpty() ? 2 : names.size();
    for (int channel = 0; channel < inputs; ++channel)
        inputBox.addItem("In " + juce::String(channel + 1) +
                             (names[channel].isNotEmpty() ? "  (" + names[channel] + ")" : ""),
                         channel + 1);
    inputBox.setSelectedId(1, juce::dontSendNotification);

    measureButton.onClick = [this] { startMeasuring(); };
    applyButton.setEnabled(false);
    applyButton.onClick = [this] {
        if (result && result->succeeded)
            session.setLatencyOffset(static_cast<int>(result->offset));
        applyButton.setEnabled(false);
        resultLabel.setText(resultLabel.getText() + "\nThe offset is set.",
                            juce::dontSendNotification);
    };

    resultLabel.setJustificationType(juce::Justification::topLeft);
    for (auto* label : {&outputLabel, &inputLabel})
        label->setJustificationType(juce::Justification::centredRight);

    for (auto* component : std::initializer_list<juce::Component*>{
             &outputLabel, &outputBox, &inputLabel, &inputBox, &measureButton, &progressBar,
             &resultLabel, &applyButton})
        addAndMakeVisible(*component);

    setSize(480, 330);
}

LatencyMeasurementDialog::~LatencyMeasurementDialog()
{
    stopTimer();
}

void LatencyMeasurementDialog::show(controller::Session& session, juce::Component* parent)
{
    launch(std::make_unique<LatencyMeasurementDialog>(session), "Measure Latency", parent);
}

void LatencyMeasurementDialog::startMeasuring()
{
    result.reset();
    applyButton.setEnabled(false);
    const auto started =
        session.measureLatency(outputBox.getSelectedId() - 1, inputBox.getSelectedId() - 1);
    if (started.failed())
    {
        resultLabel.setText(started.getErrorMessage(), juce::dontSendNotification);
        return;
    }

    resultLabel.setText("Measuring...", juce::dontSendNotification);
    measureButton.setEnabled(false);
    startTimerHz(20);
}

void LatencyMeasurementDialog::timerCallback()
{
    progress = session.getLatencyMeasurementProgress();
    result = session.takeLatencyResult();
    if (!result)
        return;

    stopTimer();
    progress = 1.0;
    measureButton.setEnabled(true);
    resultLabel.setText(result->message, juce::dontSendNotification);
    applyButton.setEnabled(result->succeeded);
}

void LatencyMeasurementDialog::resized()
{
    auto area = getLocalBounds().reduced(12);
    instructions.setBounds(area.removeFromTop(70));
    area.removeFromTop(6);

    for (auto [label, box] :
         {std::pair{&outputLabel, &outputBox}, std::pair{&inputLabel, &inputBox}})
    {
        auto line = area.removeFromTop(26);
        label->setBounds(line.removeFromLeft(70));
        line.removeFromLeft(8);
        box->setBounds(line.removeFromLeft(260));
        area.removeFromTop(6);
    }

    auto buttons = area.removeFromTop(28);
    measureButton.setBounds(buttons.removeFromLeft(120));
    buttons.removeFromLeft(8);
    progressBar.setBounds(buttons);
    area.removeFromTop(8);
    applyButton.setBounds(area.removeFromBottom(28).removeFromRight(150));
    resultLabel.setBounds(area);
}

} // namespace daw::ui::desktop
