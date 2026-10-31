#include "daw/ui/desktop/TrackInputControls.h"

namespace daw::ui::desktop
{

namespace
{

constexpr int noInputItem = 1;
constexpr int monoItems = 1000;   // + channel
constexpr int stereoItems = 2000; // + first channel of the pair

juce::String channelLabel(const juce::StringArray& names, int channel)
{
    const auto name = names[channel].trim();
    const auto number = "In " + juce::String(channel + 1);
    return name.isNotEmpty() && name != number ? number + "  (" + name + ")" : number;
}

} // namespace

juce::String describeInput(model::TrackInput input)
{
    if (input.isNone())
        return "No input";
    if (input.stereo)
        return "In " + juce::String(input.channel + 1) + "+" + juce::String(input.channel + 2);
    return "In " + juce::String(input.channel + 1);
}

juce::String describeMonitoring(model::MonitoringMode mode)
{
    switch (mode)
    {
    case model::MonitoringMode::off:
        return "Off";
    case model::MonitoringMode::automatic:
        return "Auto";
    case model::MonitoringMode::on:
        return "On";
    }
    return "Auto";
}

TrackInputControls::TrackInputControls(controller::Session& sessionToEdit, model::ObjectId id)
    : session(sessionToEdit), trackId(id)
{
    arm.setTooltip("Arm for recording (R records every armed track)");
    monitoring.setTooltip("Input monitoring. Auto: hear the input while armed and stopped, "
                          "during the count-in and while recording. On: always hear the "
                          "input. Off: never. (Click to change)");
    input.setTooltip("The device input this track records and monitors");

    arm.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd32f2f));
    monitoring.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffef6c00));

    arm.onClick = [this]
    {
        const bool armed = !arm.getToggleState();
        if (!session.setTrackArmed(trackId, armed))
            juce::NativeMessageBox::showAsync(juce::MessageBoxOptions::makeOptionsOk(
                                                  juce::MessageBoxIconType::WarningIcon,
                                                  "Microphone access is off",
                                                  controller::microphoneDeniedMessage, "OK", &arm),
                                              [](int) {});
    };

    monitoring.onClick = [this]
    {
        const auto track =
            session.getProject().getTrack(session.getProject().indexOfTrack(trackId));
        const auto next = track.getMonitoring() == model::MonitoringMode::off
                              ? model::MonitoringMode::automatic
                          : track.getMonitoring() == model::MonitoringMode::automatic
                              ? model::MonitoringMode::on
                              : model::MonitoringMode::off;
        session.setTrackMonitoring(trackId, next);
    };

    input.onClick = [this]
    {
        showInputMenu();
    };
}

void TrackInputControls::addTo(juce::Component& parent)
{
    for (auto* button : {&arm, &monitoring, &input})
    {
        button->setWantsKeyboardFocus(false);
        parent.addAndMakeVisible(*button);
    }
}

void TrackInputControls::show(const model::Track& track)
{
    arm.setToggleState(track.isArmed(), juce::dontSendNotification);
    monitoring.setButtonText(describeMonitoring(track.getMonitoring()));
    monitoring.setToggleState(track.getMonitoring() == model::MonitoringMode::on,
                              juce::dontSendNotification);
    input.setButtonText(describeInput(track.getInput()));
}

void TrackInputControls::showInputMenu()
{
    const auto index = session.getProject().indexOfTrack(trackId);
    if (index < 0)
        return;
    const auto current = session.getProject().getTrack(index).getInput();

    // The device's channels; before any input is open, the usual first two.
    const auto& names = session.getInputChannelNames();
    const int numChannels =
        names.isEmpty() ? 2 : std::min(names.size(), model::TrackInput::maximumChannels);

    juce::PopupMenu mono;
    juce::PopupMenu stereo;
    for (int channel = 0; channel < numChannels; ++channel)
    {
        mono.addItem(monoItems + channel, channelLabel(names, channel), true,
                     !current.isNone() && !current.stereo && current.channel == channel);
        if (channel % 2 == 0 && channel + 1 < numChannels)
            stereo.addItem(stereoItems + channel,
                           "In " + juce::String(channel + 1) + "+" + juce::String(channel + 2),
                           true, current.stereo && current.channel == channel);
    }

    juce::PopupMenu menu;
    menu.addItem(noInputItem, "No input", true, current.isNone());
    menu.addSeparator();
    menu.addSubMenu("Mono", mono);
    menu.addSubMenu("Stereo", stereo, stereo.getNumItems() > 0);
    if (names.isEmpty())
        menu.addItem(-1, "(The inputs open once a track needs them)", false);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&input),
                       [this](int result)
                       {
                           if (result == noInputItem)
                               session.setTrackInput(trackId, model::TrackInput::none());
                           else if (result >= stereoItems)
                               session.setTrackInput(trackId, {result - stereoItems, true});
                           else if (result >= monoItems)
                               session.setTrackInput(trackId, {result - monoItems, false});
                       });
}

} // namespace daw::ui::desktop
