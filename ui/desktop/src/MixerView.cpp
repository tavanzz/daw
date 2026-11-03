#include "daw/ui/desktop/MixerView.h"

#include "daw/engine/dsp/FaderLaw.h"
#include "daw/ui/desktop/EffectSlots.h"
#include "daw/ui/desktop/SendSlots.h"
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

// One mixer strip: a track's, a bus's (no solo, no input) or the master's (no pan,
// mute or solo). Every strip has its insert effects; a track's also its sends.
class MixerView::Strip final : public juce::Component
{
public:
    Strip(controller::Session& sessionToEdit, controller::ChainOwner chainOwner,
          const std::function<void(model::ObjectId)>& openEffect)
        : session(sessionToEdit)
        , owner(chainOwner)
        , inserts(sessionToEdit, chainOwner)
    {
        const bool isTrack = owner.kind == controller::ChainOwner::Kind::track;
        const bool isBus = owner.kind == controller::ChainOwner::Kind::bus;

        name.setJustificationType(juce::Justification::centred);
        name.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        if (isBus)
        {
            name.setEditable(false, true, false);
            name.onTextChange = [this] { session.renameBus(owner.id, name.getText()); };
            name.addMouseListener(this, false);
        }
        addAndMakeVisible(name);

        inserts.onOpen = [&openEffect](model::ObjectId id) {
            if (openEffect)
                openEffect(id);
        };
        addAndMakeVisible(inserts);

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
            switch (owner.kind)
            {
            case controller::ChainOwner::Kind::track:
                session.setTrackVolume(owner.id, gain);
                break;
            case controller::ChainOwner::Kind::bus:
                session.setBusVolume(owner.id, gain);
                break;
            case controller::ChainOwner::Kind::master:
                session.setMasterVolume(gain);
                break;
            }
        };
        fader.updateText(); // the value never changed, so the text box needs telling
        addAndMakeVisible(fader);

        meter.setScaleVisible(false);
        addAndMakeVisible(meter);

        if (owner.kind == controller::ChainOwner::Kind::master)
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
        pan.onValueChange = [this] {
            if (owner.kind == controller::ChainOwner::Kind::track)
                session.setTrackPan(owner.id, pan.getValue());
            else
                session.setBusPan(owner.id, pan.getValue());
        };
        pan.updateText();
        addAndMakeVisible(pan);

        mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffc62828));
        mute.setWantsKeyboardFocus(false);
        addAndMakeVisible(mute);
        mute.onClick = [this] {
            if (owner.kind == controller::ChainOwner::Kind::track)
                session.setTrackMuted(owner.id, !mute.getToggleState());
            else
                session.setBusMuted(owner.id, !mute.getToggleState());
        };

        if (!isTrack)
            return;

        solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xfff9a825));
        solo.setWantsKeyboardFocus(false);
        addAndMakeVisible(solo);
        solo.onClick = [this] { session.setTrackSoloed(owner.id, !solo.getToggleState()); };

        sends = std::make_unique<SendSlots>(session, owner.id);
        addAndMakeVisible(*sends);

        recording = std::make_unique<TrackInputControls>(session, owner.id);
        recording->addTo(*this);
    }

    [[nodiscard]] const controller::ChainOwner& getOwner() const noexcept { return owner; }

    /** Shows a track's recording settings. */
    void showRecording(const model::Track& track)
    {
        if (recording != nullptr)
            recording->show(track);
    }

    /** Shows the model's values, except on a control the user is dragging; automated
        ones follow their automation and cannot be moved. */
    void show(const juce::String& stripName, double volume, double panPosition, bool muted,
              bool soloed, bool projectChanged)
    {
        name.setText(stripName, juce::dontSendNotification);

        const auto automatedVolume = session.getAutomatedValue(owner, model::AutomationTarget::volume());
        fader.setEnabled(!automatedVolume.has_value());
        if (automatedVolume)
            fader.setValue(engine::dsp::faderFloorDecibels +
                               (engine::dsp::faderTopDecibels - engine::dsp::faderFloorDecibels) *
                                   *automatedVolume,
                           juce::dontSendNotification);
        else if (!fader.isMouseButtonDown())
            fader.setValue(gainToFaderDb(volume), juce::dontSendNotification);

        if (owner.kind != controller::ChainOwner::Kind::master)
        {
            const auto automatedPan = session.getAutomatedValue(owner, model::AutomationTarget::pan());
            pan.setEnabled(!automatedPan.has_value());
            if (automatedPan)
                pan.setValue(2.0 * *automatedPan - 1.0, juce::dontSendNotification);
            else if (!pan.isMouseButtonDown())
                pan.setValue(panPosition, juce::dontSendNotification);
        }

        mute.setToggleState(muted, juce::dontSendNotification);
        solo.setToggleState(soloed, juce::dontSendNotification);
        inserts.refresh(projectChanged);
        if (sends != nullptr)
            sends->refresh(projectChanged);
    }

    void updateMeter(float left, float right, double elapsedSeconds)
    {
        meter.update(left, right, elapsedSeconds);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        // Right-click a bus's name to delete the bus.
        if (event.eventComponent != &name || !event.mods.isPopupMenu() ||
            owner.kind != controller::ChainOwner::Kind::bus)
            return;

        juce::PopupMenu menu;
        menu.addItem(1, "Delete Bus");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&name), [this](int choice) {
            if (choice == 1)
                session.removeBus(owner.id);
        });
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        name.setBounds(area.removeFromTop(20));
        area.removeFromTop(4);

        if (recording != nullptr)
        {
            auto inputRow = area.removeFromTop(22);
            recording->monitoring.setBounds(inputRow.removeFromRight(42).reduced(1, 0));
            recording->input.setBounds(inputRow.reduced(1, 0));
            area.removeFromTop(4);
        }

        inserts.setBounds(area.removeFromTop(EffectSlots::rowHeight * 5));
        area.removeFromTop(4);
        if (sends != nullptr)
        {
            sends->setBounds(area.removeFromTop(SendSlots::rowHeight * 3));
            area.removeFromTop(4);
        }
        else
        {
            area.removeFromTop(SendSlots::rowHeight * 3 + 4);
        }

        if (owner.kind != controller::ChainOwner::Kind::master)
        {
            pan.setBounds(area.removeFromTop(60));
            auto buttons = area.removeFromTop(24);
            const int third = buttons.getWidth() / 3;
            mute.setBounds(buttons.removeFromLeft(third).reduced(2, 0));
            if (owner.kind == controller::ChainOwner::Kind::track)
            {
                solo.setBounds(buttons.removeFromLeft(third).reduced(2, 0));
                recording->arm.setBounds(buttons.reduced(2, 0));
            }
            area.removeFromTop(6);
        }

        meter.setBounds(area.removeFromRight(18).withTrimmedBottom(20));
        fader.setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        const auto colour = owner.kind == controller::ChainOwner::Kind::track
                                ? juce::Colour(0xff262626)
                                : (owner.kind == controller::ChainOwner::Kind::bus
                                       ? juce::Colour(0xff222a33)
                                       : juce::Colour(0xff2e2a24));
        g.fillAll(colour);
        g.setColour(juce::Colour(0xff1a1a1a));
        g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    }

private:
    controller::Session& session;
    const controller::ChainOwner owner;
    juce::Label name;
    EffectSlots inserts;
    std::unique_ptr<SendSlots> sends;
    juce::Slider fader;
    juce::Slider pan;
    juce::TextButton mute{"M"};
    juce::TextButton solo{"S"};
    std::unique_ptr<TrackInputControls> recording;
    LevelMeter meter;
};

MixerView::MixerView(controller::Session& sessionToEdit)
    : session(sessionToEdit)
    , masterStrip(std::make_unique<Strip>(sessionToEdit, controller::ChainOwner::master(),
                                          openEffect))
{
    stripViewport.setViewedComponent(&stripHolder, false);
    stripViewport.setScrollBarsShown(false, true);
    addAndMakeVisible(stripViewport);
    addAndMakeVisible(*masterStrip);
    rebuildStrips(true);
}

MixerView::~MixerView() = default;

void MixerView::rebuildStrips(bool projectChanged)
{
    const auto& project = session.getProject();

    // Tracks, then buses.
    std::vector<controller::ChainOwner> owners;
    for (int i = 0; i < project.getNumTracks(); ++i)
        owners.push_back(controller::ChainOwner::track(project.getTrack(i).getId()));
    for (int i = 0; i < project.getNumBuses(); ++i)
        owners.push_back(controller::ChainOwner::bus(project.getBus(i).getId()));

    bool same = owners.size() == strips.size();
    for (std::size_t i = 0; same && i < owners.size(); ++i)
        same = strips[i]->getOwner() == owners[i];

    if (!same)
    {
        strips.clear();
        for (const auto& owner : owners)
        {
            strips.push_back(std::make_unique<Strip>(session, owner, openEffect));
            stripHolder.addAndMakeVisible(*strips.back());
        }
        resized();
    }

    for (int i = 0; i < project.getNumTracks(); ++i)
    {
        const auto track = project.getTrack(i);
        auto& strip = *strips[static_cast<std::size_t>(i)];
        strip.show(track.getName(), track.getVolume(), track.getPan(), track.isMuted(),
                   track.isSoloed(), projectChanged);
        strip.showRecording(track);
    }
    for (int i = 0; i < project.getNumBuses(); ++i)
    {
        const auto bus = project.getBus(i);
        strips[static_cast<std::size_t>(project.getNumTracks() + i)]->show(
            bus.getName(), bus.getVolume(), bus.getPan(), bus.isMuted(), false, projectChanged);
    }

    masterStrip->show("Master", project.getMasterVolume(), 0.0, false, false, projectChanged);
}

void MixerView::refresh(float masterLeftPeak, float masterRightPeak, double elapsedSeconds)
{
    const bool projectChanged = shownRevision != session.getRevision();
    shownRevision = session.getRevision();
    rebuildStrips(projectChanged);

    for (auto& strip : strips)
    {
        const auto meter = session.getTrackMeter(strip->getOwner().id); // buses share the map
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
