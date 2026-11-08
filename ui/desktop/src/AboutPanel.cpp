#include "daw/ui/desktop/AboutPanel.h"

#include "daw/model/FormatVersion.h"
#include "daw/model/NetworkProtocol.h"
#include "daw/services/BuildInfo.h"
#include "daw/services/UserSettings.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

namespace
{
using appearance::ColourRole;
using appearance::FontRole;

constexpr int margin = 20;
} // namespace

AboutPanel::AboutPanel(const juce::String& signature, const juce::File& licencesFolder)
{
    const auto& build = services::buildInfo();

    name.setText(build.productName, juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(name, FontRole::title);
    version.setText("Version " + build.version + (build.buildType == "Release" ? juce::String() : " (" + build.buildType + ")"),
                    juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(version, FontRole::heading);

    juce::StringArray lines;
    lines.add("Commit " + build.commit + (build.modified ? " with local changes" : juce::String()) +
              (build.commitDate.isNotEmpty() ? ", " + build.commitDate.upToFirstOccurrenceOf("T", false, false)
                                             : juce::String()));
    lines.add(build.platform + ", " + build.architecture);
    lines.add("Project files: format " + juce::String(model::currentFormatVersion) + "    Network protocol: " +
              juce::String(model::networkProtocolVersion) + "    Settings: version " +
              juce::String(services::currentSettingsVersion));
    if (signature.isNotEmpty())
        lines.add(signature);
    details.setText(lines.joinIntoString("\n"), juce::dontSendNotification);
    details.setJustificationType(juce::Justification::topLeft);
    ThemedLookAndFeel::setFontRole(details, FontRole::caption);
    ThemedLookAndFeel::setColourRole(details, ColourRole::textSecondary);

    trademark.setText("VST is a registered trademark of Steinberg Media Technologies GmbH.",
                      juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(trademark, FontRole::caption);
    ThemedLookAndFeel::setColourRole(trademark, ColourRole::textSecondary);

    repository.setButtonText("Source code and issues");
    repository.setURL(juce::URL(build.repositoryUrl));
    releases.setButtonText("All releases and their notes");
    releases.setURL(juce::URL(build.repositoryUrl + "/releases"));
    privacy.setButtonText("Privacy: what the app sends");
    privacy.setURL(juce::URL(build.repositoryUrl + "/blob/main/docs/PRIVACY.md"));
    for (auto* link : {&repository, &releases, &privacy})
        link->setJustificationType(juce::Justification::centredLeft);

    licencesLabel.setText("Licences", juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(licencesLabel, FontRole::heading);

    // The app's own licence, the list of components, then each component's licence.
    for (const auto& [title, fileName] : {std::pair{build.productName, "LICENSE.txt"},
                                          std::pair{juce::String("Third-party components"), "THIRD_PARTY.md"}})
        if (const auto file = licencesFolder.getChildFile(fileName); file.existsAsFile())
            licences.push_back({title, file});
    auto folders = licencesFolder.findChildFiles(juce::File::findDirectories, false);
    folders.sort();
    for (const auto& folder : folders)
        for (const auto& file : folder.findChildFiles(juce::File::findFiles, false))
            licences.push_back({folder.getFileName() + (file.getFileName().startsWith("OFL") ? " (font)" : ""), file});

    for (size_t index = 0; index < licences.size(); ++index)
        licenceChoice.addItem(licences[index].name, static_cast<int>(index) + 1);
    licenceChoice.onChange = [this] { showLicence(licenceChoice.getSelectedItemIndex()); };

    licenceText.setMultiLine(true, true);
    licenceText.setReadOnly(true);
    licenceText.setScrollbarsShown(true);
    licenceText.setCaretVisible(false);

    for (auto* component : std::initializer_list<juce::Component*>{&name, &version, &details, &trademark, &repository,
                                                                   &releases, &privacy, &licencesLabel,
                                                                   &licenceChoice, &licenceText})
        addAndMakeVisible(*component);

    lookAndFeelChanged();
    if (licences.empty())
        licenceText.setText("The licence files are not with this copy of the app.");
    else
        licenceChoice.setSelectedItemIndex(0, juce::sendNotificationSync);
    setSize(620, 640);
}

juce::StringArray AboutPanel::getLicenceNames() const
{
    juce::StringArray names;
    for (const auto& licence : licences)
        names.add(licence.name);
    return names;
}

void AboutPanel::showLicence(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= licences.size())
        return;
    licenceChoice.setSelectedItemIndex(index, juce::dontSendNotification);
    licenceText.setText(licences[static_cast<size_t>(index)].file.loadFileAsString(), false);
    licenceText.moveCaretToTop(false);
}

void AboutPanel::lookAndFeelChanged()
{
    // The text editor takes its font from the theme here: it has no font role of its own.
    if (auto* look = dynamic_cast<ThemedLookAndFeel*>(&getLookAndFeel()))
        licenceText.applyFontToAllText(look->font(FontRole::numeric), true);
}

void AboutPanel::paint(juce::Graphics& g)
{
    g.fillAll(ThemedLookAndFeel::get(*this).colour(ColourRole::panel));
}

void AboutPanel::resized()
{
    auto area = getLocalBounds().reduced(margin);
    name.setBounds(area.removeFromTop(40));
    version.setBounds(area.removeFromTop(26));
    details.setBounds(area.removeFromTop(74));
    area.removeFromTop(4);
    for (auto* link : {&repository, &releases, &privacy})
        link->setBounds(area.removeFromTop(22).withWidth(320));
    area.removeFromTop(10);
    trademark.setBounds(area.removeFromBottom(22));
    auto header = area.removeFromTop(28);
    licencesLabel.setBounds(header.removeFromLeft(110));
    licenceChoice.setBounds(header.withWidth(300));
    area.removeFromTop(6);
    licenceText.setBounds(area);
}

} // namespace daw::ui::desktop
