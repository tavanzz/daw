#pragma once

#include "daw/controller/ExportJob.h"
#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace daw::ui::desktop
{

/**
    The export dialog: range (whole project or loop region), sample rate, bit depth
    and dither. Export asks for the file, then writes it on a background thread with
    a cancellable progress window, and reports the result (warning if the mix went
    over 0 dBFS).
*/
class ExportDialog final : public juce::Component
{
public:
    explicit ExportDialog(controller::Session& sessionToExport);
    ~ExportDialog() override;

    /** Shows the dialog in its own window, centred on parent. */
    static void show(controller::Session& session, juce::Component* parent);

    void resized() override;

private:
    [[nodiscard]] controller::ExportSettings chosenSettings() const;
    void chooseFileAndExport();
    void closeWindow();

    controller::Session& session;
    juce::Label rangeLabel{{}, "Range"};
    juce::Label rateLabel{{}, "Sample rate"};
    juce::Label bitsLabel{{}, "Format"};
    juce::ComboBox rangeBox;
    juce::ComboBox rateBox;
    juce::ComboBox bitsBox;
    juce::ToggleButton ditherButton{"Dither (TPDF)"};
    juce::TextButton exportButton{"Export..."};
    juce::TextButton cancelButton{"Cancel"};
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportDialog)
};

} // namespace daw::ui::desktop
