#include "daw/ui/desktop/ExportDialog.h"

#include <cmath>

namespace daw::ui::desktop
{

namespace
{

// Runs an export on its own thread behind a progress window, then reports.
class ExportProgress final : public juce::ThreadWithProgressWindow
{
public:
    ExportProgress(controller::Session& session, controller::ExportSettings settings)
        : ThreadWithProgressWindow("Exporting \"" + settings.file.getFileName() + "\"", true, true)
        , job(session, std::move(settings))
    {
    }

    void run() override
    {
        result = job.run([this](double fraction) {
            setProgress(fraction);
            return !threadShouldExit();
        });
    }

    void threadComplete(bool userPressedCancel) override
    {
        const auto& settings = job.getSettings();
        juce::String title = "Export finished";
        juce::String text;
        auto icon = juce::MessageBoxIconType::InfoIcon;

        if (userPressedCancel || result.cancelled)
        {
            title = "Export cancelled";
            text = "Nothing was written.";
        }
        else if (result.result.failed())
        {
            title = "Export failed";
            text = result.result.getErrorMessage();
            icon = juce::MessageBoxIconType::WarningIcon;
        }
        else
        {
            const double seconds = static_cast<double>(result.samplesWritten) / settings.sampleRate;
            const auto format = settings.bitsPerSample == 32
                                    ? juce::String("32-bit float")
                                    : juce::String(settings.bitsPerSample) + "-bit";
            const auto peak = result.peak > 0.0f
                                  ? juce::String(20.0 * std::log10(result.peak), 1) + " dBFS"
                                  : juce::String("silence");
            text << settings.file.getFullPathName() << "\n" << juce::String(seconds, 2)
                 << " s at " << juce::String(settings.sampleRate / 1000.0, 1) << " kHz, "
                 << format << ". Peak " << peak << ".";

            if (result.peak > 1.0f)
            {
                icon = juce::MessageBoxIconType::WarningIcon;
                text << "\n\nThe mix goes over 0 dBFS: ";
                if (settings.bitsPerSample == 32)
                    text << "the float file keeps it, but it will clip when played or "
                            "converted.";
                else
                    text << "it is clipped in the file. Lower the master volume and export "
                            "again.";
            }
        }

        juce::NativeMessageBox::showAsync(
            juce::MessageBoxOptions::makeOptionsOk(icon, title, text), nullptr);
        delete this; // launched with new, owns itself
    }

private:
    controller::ExportJob job;
    engine::io::BounceResult result;
};

} // namespace

ExportDialog::ExportDialog(controller::Session& sessionToExport)
    : session(sessionToExport)
{
    const bool hasLoop = session.getProject().getLoopEnd() > session.getProject().getLoopStart();
    rangeBox.addItem("Whole project", 1);
    rangeBox.addItem("Loop region", 2);
    rangeBox.setItemEnabled(2, hasLoop);
    rangeBox.setSelectedId(1, juce::dontSendNotification);

    int rateId = 2; // 48 kHz unless the device runs at one of the others
    for (int i = 0; i < static_cast<int>(std::size(controller::exportSampleRates)); ++i)
    {
        const double rate = controller::exportSampleRates[i];
        rateBox.addItem(juce::String(rate / 1000.0, 1) + " kHz", i + 1);
        if (juce::exactlyEqual(rate, session.getSampleRate()))
            rateId = i + 1;
    }
    rateBox.setSelectedId(rateId, juce::dontSendNotification);

    bitsBox.addItem("WAV 16-bit", 16);
    bitsBox.addItem("WAV 24-bit", 24);
    bitsBox.addItem("WAV 32-bit float", 32);
    bitsBox.setSelectedId(24, juce::dontSendNotification);
    bitsBox.onChange = [this] {
        const bool sixteen = bitsBox.getSelectedId() == 16;
        ditherButton.setEnabled(sixteen);
        ditherButton.setToggleState(sixteen, juce::dontSendNotification);
    };
    ditherButton.setEnabled(false);
    ditherButton.setTooltip("Adds very quiet noise before reducing to 16 bits, which avoids "
                            "distortion in fades and quiet passages");

    for (auto* label : {&rangeLabel, &rateLabel, &bitsLabel})
        addAndMakeVisible(*label);
    for (auto* box : {&rangeBox, &rateBox, &bitsBox})
        addAndMakeVisible(*box);
    addAndMakeVisible(ditherButton);
    addAndMakeVisible(exportButton);
    addAndMakeVisible(cancelButton);

    exportButton.onClick = [this] { chooseFileAndExport(); };
    cancelButton.onClick = [this] { closeWindow(); };

    setSize(380, 210);
}

ExportDialog::~ExportDialog() = default;

void ExportDialog::show(controller::Session& session, juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new ExportDialog(session));
    options.dialogTitle = "Export Mix";
    options.dialogBackgroundColour =
        parent->getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = parent;
    options.launchAsync();
}

controller::ExportSettings ExportDialog::chosenSettings() const
{
    controller::ExportSettings settings;
    using Range = controller::ExportSettings::Range;
    settings.range = rangeBox.getSelectedId() == 2 ? Range::loopRegion : Range::wholeProject;
    settings.sampleRate = controller::exportSampleRates[rateBox.getSelectedId() - 1];
    settings.bitsPerSample = bitsBox.getSelectedId();
    settings.dither = settings.bitsPerSample == 16 && ditherButton.getToggleState();
    return settings;
}

void ExportDialog::chooseFileAndExport()
{
    const auto projectFile = session.getProjectFile();
    const auto suggested = projectFile != juce::File()
                               ? projectFile.withFileExtension(".wav")
                               : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                     .getChildFile("Mix.wav");

    fileChooser = std::make_unique<juce::FileChooser>("Export the mix", suggested, "*.wav");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                 juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::warnAboutOverwriting,
                             [this](const juce::FileChooser& chooser) {
                                 auto file = chooser.getResult();
                                 if (file == juce::File())
                                     return;

                                 auto settings = chosenSettings();
                                 settings.file = file.withFileExtension(".wav");
                                 (new ExportProgress(session, settings))->launchThread();
                                 closeWindow();
                             });
}

void ExportDialog::closeWindow()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}

void ExportDialog::resized()
{
    auto area = getLocalBounds().reduced(16);
    const auto row = [&area] {
        auto line = area.removeFromTop(26);
        area.removeFromTop(8);
        return line;
    };

    auto line = row();
    rangeLabel.setBounds(line.removeFromLeft(100));
    rangeBox.setBounds(line);
    line = row();
    rateLabel.setBounds(line.removeFromLeft(100));
    rateBox.setBounds(line);
    line = row();
    bitsLabel.setBounds(line.removeFromLeft(100));
    bitsBox.setBounds(line);
    line = row();
    line.removeFromLeft(100);
    ditherButton.setBounds(line);

    auto buttons = area.removeFromBottom(28);
    exportButton.setBounds(buttons.removeFromRight(100));
    buttons.removeFromRight(8);
    cancelButton.setBounds(buttons.removeFromRight(100));
}

} // namespace daw::ui::desktop
