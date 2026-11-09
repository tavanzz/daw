#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace daw::ui::desktop
{

/**
    Help > About: the app's version, the commit it was built from, the platform, the
    versions of its file formats and network protocol, how it is signed; its licence and
    those of the third-party components it ships with (from the Licences folder), and
    links to the repository, the releases and the privacy notes.
*/
class AboutPanel final : public juce::Component
{
public:
    /** `signature`: how this copy is signed; `licencesFolder`: the shipped licences. */
    AboutPanel(const juce::String& signature, const juce::File& licencesFolder);

    // For tests.
    [[nodiscard]] juce::StringArray getLicenceNames() const;
    void showLicence(int index);
    [[nodiscard]] juce::String getShownLicence() const { return licenceText.getText(); }
    [[nodiscard]] juce::String getDetails() const { return details.getText(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    struct Licence
    {
        juce::String name;
        juce::File file;
    };

    juce::Label name, version, details, trademark;
    juce::HyperlinkButton repository, releases, privacy;
    juce::Label licencesLabel;
    juce::ComboBox licenceChoice;
    juce::TextEditor licenceText;
    std::vector<Licence> licences;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutPanel)
};

} // namespace daw::ui::desktop
