#include "daw/ui/desktop/MixerView.h"

#include "daw/ui/desktop/TrackInputControls.h"

#include <cmath>

namespace daw::ui::desktop
{

namespace
{
constexpr double faderFloorDb = -60.0; // the bottom of the fader: silence
constexpr double faderTopDb = 6.0;
constexpr int stripWidth = 92;

juce::String formatDb(double decibels)
{
    if (decibels <= faderFloorDb)
        return "-inf";
    return (decibels > 0.0 ? "+" : "") + juce::String(decibels, 1) + " dB";
}

juce::String formatPan(double pan)
{
    const auto percent = static_cast<int>(std::lround(std::abs(pan) * 100.0));
    if (percent == 0)
        return "C";
    return (pan < 0.0 ? "L" : "R") + juce::String(percent);
}
} // namespace

double faderDbToGain(double decibels) noexcept
{
    return decibels <= faderFloorDb ? 0.0 : std::pow(10.0, decibels / 20.0);
}

double gainToFaderDb(double gain) noexcept
{
    return gain <= 0.0 ? faderFloorDb
                       : juce::jlimit(faderFloorDb, faderTopDb, 20.0 * std::log10(gain));
}

// One mixer strip: a track's, or the master's (no pan, mute or solo).
class MixerView::Strip final : public juce::Component
{
public:
    Strip(controller::Session& sessionToEdit, std::optional<model::ObjectId> id)
        : session(sessionToEdit)
        , trackId(id)
    {
        name.setJustificationType(juce::Justification::centred);
        name.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        addAndMakeVisible(name);

        fader.setSliderStyle(juce::Slider::LinearVertical);
        fader.setRange(faderFloorDb, faderTopDb, 0.1);
        fader.setSkewFactorFromMidPoint(-12.0);
        fader.setDoubleClickReturnValue(true, 0.0);
        fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        fader.textFromValueFunction = formatDb;
        fader.valueFromTextFunction = [](const juce::String& text) {
            return text.trim().startsWithIgnoreCase("-inf") ? faderFloorDb : text.getDoubleValue();
        };
        fader.setWantsKeyboardFocus(false);
        fader.onDragStart = [this] { session.beginGesture("Volume"); };
        fader.onDragEnd = [this] { session.endGesture(); };
        fader.onValueChange = [this] {
            const double gain = faderDbToGain(fader.getValue());
            if (trackId)
                session.setTrackVolume(*trackId, gain);
            else
                session.setMasterVolume(gain);
        };
        fader.updateText(); // the value never changed, so the text box needs telling
        addAndMakeVisible(fader);

        meter.setScaleVisible(false);
        addAndMakeVisible(meter);

        if (!trackId)
            return;

        pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setRange(-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue(true, 0.0);
        pan.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
        pan.textFromValueFunction = formatPan;
        pan.valueFromTextFunction = [](const juce::String& text) {
            const auto value = text.trim();
            const double amount = value.substring(1).getDoubleValue() / 100.0;
            if (value.startsWithIgnoreCase("L"))
                return -amount;
            return value.startsWithIgnoreCase("R") ? amount : 0.0;
        };
        pan.setWantsKeyboardFocus(false);
        pan.onDragStart = [this] { session.beginGesture("Pan"); };
        pan.onDragEnd = [this] { session.endGesture(); };
        pan.onValueChange = [this] { session.setTrackPan(*trackId, pan.getValue()); };
        pan.updateText();
        addAndMakeVisible(pan);

        mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffc62828));
        solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xfff9a825));
        for (auto* button : {&mute, &solo})
        {
            button->setWantsKeyboardFocus(false);
            addAndMakeVisible(*button);
        }
        mute.onClick = [this] { session.setTrackMuted(*trackId, !mute.getToggleState()); };
        solo.onClick = [this] { session.setTrackSoloed(*trackId, !solo.getToggleState()); };

        recording = std::make_unique<TrackInputControls>(session, *trackId);
        recording->addTo(*this);
    }

    [[nodiscard]] std::optional<model::ObjectId> getTrackId() const noexcept { return trackId; }

    /** Shows a track's recording settings. */
    void showRecording(const model::Track& track)
    {
        if (recording != nullptr)
            recording->show(track);
    }

    /** Shows the model's values, except on a control the user is dragging. */
    void show(const juce::String& stripName, double volume, double panPosition, bool muted,
              bool soloed)
    {
        name.setText(stripName, juce::dontSendNotification);
        if (!fader.isMouseButtonDown())
            fader.setValue(gainToFaderDb(volume), juce::dontSendNotification);
        if (trackId && !pan.isMouseButtonDown())
            pan.setValue(panPosition, juce::dontSendNotification);
        mute.setToggleState(muted, juce::dontSendNotification);
        solo.setToggleState(soloed, juce::dontSendNotification);
    }

    void updateMeter(float left, float right, double elapsedSeconds)
    {
        meter.update(left, right, elapsedSeconds);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        name.setBounds(area.removeFromTop(20));
        area.removeFromTop(4);

        if (trackId)
        {
            auto inputRow = area.removeFromTop(22);
            recording->monitoring.setBounds(inputRow.removeFromRight(34).reduced(1, 0));
            recording->input.setBounds(inputRow.reduced(1, 0));
            area.removeFromTop(4);
            pan.setBounds(area.removeFromTop(60));
            auto buttons = area.removeFromTop(24);
            const int third = buttons.getWidth() / 3;
            mute.setBounds(buttons.removeFromLeft(third).reduced(2, 0));
            solo.setBounds(buttons.removeFromLeft(third).reduced(2, 0));
            recording->arm.setBounds(buttons.reduced(2, 0));
            area.removeFromTop(6);
        }

        meter.setBounds(area.removeFromRight(18).withTrimmedBottom(20));
        fader.setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(trackId ? juce::Colour(0xff262626) : juce::Colour(0xff2e2a24));
        g.setColour(juce::Colour(0xff1a1a1a));
        g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    }

private:
    controller::Session& session;
    const std::optional<model::ObjectId> trackId;
    juce::Label name;
    juce::Slider fader;
    juce::Slider pan;
    juce::TextButton mute{"M"};
    juce::TextButton solo{"S"};
    std::unique_ptr<TrackInputControls> recording;
    LevelMeter meter;
};

MixerView::MixerView(controller::Session& sessionToEdit)
    : session(sessionToEdit)
    , masterStrip(std::make_unique<Strip>(sessionToEdit, std::nullopt))
{
    stripViewport.setViewedComponent(&stripHolder, false);
    stripViewport.setScrollBarsShown(false, true);
    addAndMakeVisible(stripViewport);
    addAndMakeVisible(*masterStrip);
    rebuildStrips();
}

MixerView::~MixerView() = default;

void MixerView::rebuildStrips()
{
    const auto& project = session.getProject();

    bool sameTracks = static_cast<int>(strips.size()) == project.getNumTracks();
    for (int i = 0; sameTracks && i < project.getNumTracks(); ++i)
        sameTracks =
            strips[static_cast<std::size_t>(i)]->getTrackId() == project.getTrack(i).getId();

    if (!sameTracks)
    {
        strips.clear();
        for (int i = 0; i < project.getNumTracks(); ++i)
        {
            strips.push_back(std::make_unique<Strip>(session, project.getTrack(i).getId()));
            stripHolder.addAndMakeVisible(*strips.back());
        }
        resized();
    }

    for (int i = 0; i < project.getNumTracks(); ++i)
    {
        const auto track = project.getTrack(i);
        auto& strip = *strips[static_cast<std::size_t>(i)];
        strip.show(track.getName(), track.getVolume(), track.getPan(), track.isMuted(),
                   track.isSoloed());
        strip.showRecording(track);
    }

    masterStrip->show("Master", project.getMasterVolume(), 0.0, false, false);
}

void MixerView::refresh(float masterLeftPeak, float masterRightPeak, double elapsedSeconds)
{
    if (shownRevision != session.getRevision())
    {
        shownRevision = session.getRevision();
        rebuildStrips();
    }

    for (auto& strip : strips)
    {
        const auto meter = session.getTrackMeter(*strip->getTrackId());
        strip->updateMeter(meter->takePeak(0), meter->takePeak(1), elapsedSeconds);
    }

    masterStrip->updateMeter(masterLeftPeak, masterRightPeak, elapsedSeconds);
}

void MixerView::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(4);
    masterStrip->setBounds(area.removeFromRight(stripWidth));
    area.removeFromRight(6);
    stripViewport.setBounds(area);

    const int height = area.getHeight() - stripViewport.getScrollBarThickness();
    stripHolder.setSize(std::max(area.getWidth(), static_cast<int>(strips.size()) * stripWidth),
                        height);
    for (std::size_t i = 0; i < strips.size(); ++i)
        strips[i]->setBounds(static_cast<int>(i) * stripWidth, 0, stripWidth, height);
}

void MixerView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

} // namespace daw::ui::desktop
