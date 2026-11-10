#include "daw/ui/desktop/TrackHeaderList.h"

#include "daw/ui/desktop/ArrangeState.h"
#include "daw/ui/desktop/AutomationLanes.h"
#include "daw/ui/desktop/CoopPanels.h"
#include "daw/ui/desktop/LevelMeter.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"
#include "daw/ui/desktop/TrackInputControls.h"

namespace daw::ui::desktop
{

// The lane's target and the owner's automation mode, for a row of the automation view.
class TrackHeaderList::AutomationControls
{
public:
    AutomationControls(ArrangeState& sharedState, controller::ChainOwner chainOwner)
        : state(sharedState)
        , owner(chainOwner)
    {
        target.setTooltip("What the automation lane shows (* : it has points)");
        target.onClick = [this] {
            AutomationLanes::showTargetMenu(state.session, owner, &target,
                                            [this](const model::AutomationTarget& chosen) {
                                                state.automationTargets[owner] = chosen;
                                                state.viewChanged();
                                                show();
                                            });
        };
        mode.setTooltip("Read: the automation moves its targets; Off: it is ignored");
        mode.setClickingTogglesState(false);
        ThemedLookAndFeel::setColourRole(mode, appearance::ColourRole::automation);
        mode.onClick = [this] {
            const bool reads = state.session.getAutomation(owner).getMode() == model::AutomationMode::read;
            state.session.setAutomationMode(owner, reads ? model::AutomationMode::off
                                                         : model::AutomationMode::read);
            show();
        };
        for (auto* button : {&target, &mode})
            button->setWantsKeyboardFocus(false);
    }

    void addTo(juce::Component& parent)
    {
        parent.addChildComponent(target);
        parent.addChildComponent(mode);
    }

    void setVisible(bool visible)
    {
        target.setVisible(visible);
        mode.setVisible(visible);
    }

    void show()
    {
        target.setButtonText(AutomationLanes::targetName(state.session, owner, state.targetOf(owner)));
        const bool reads = state.session.getAutomation(owner).getMode() == model::AutomationMode::read;
        mode.setButtonText(reads ? "Read" : "Off");
        mode.setToggleState(reads, juce::dontSendNotification);
    }

    void place(juce::Rectangle<int> row)
    {
        mode.setBounds(row.removeFromRight(46));
        row.removeFromRight(4);
        target.setBounds(row);
    }

private:
    ArrangeState& state;
    const controller::ChainOwner owner;
    juce::TextButton target;
    juce::TextButton mode;
};

// A bus's or the master's row header, in the automation view.
class TrackHeaderList::OwnerHeader final : public juce::Component
{
public:
    OwnerHeader(ArrangeState& sharedState, controller::ChainOwner chainOwner)
        : owner(chainOwner)
        , controls(sharedState, chainOwner)
    {
        ThemedLookAndFeel::setFontRole(name, appearance::FontRole::heading);
        name.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(name);
        controls.addTo(*this);
        controls.setVisible(true);
    }

    [[nodiscard]] const controller::ChainOwner& getOwner() const noexcept { return owner; }

    void show(const juce::String& text)
    {
        name.setText(text, juce::dontSendNotification);
        controls.show();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8, 6);
        name.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        controls.place(area.removeFromTop(24));
    }

    void paint(juce::Graphics& g) override
    {
        ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(),
                                                appearance::ColourRole::panelAlt, true);
    }

private:
    const controller::ChainOwner owner;
    juce::Label name;
    AutomationControls controls;
};

class TrackHeaderList::Header final : public juce::Component
{
public:
    Header(ArrangeState& sharedState, model::ObjectId id)
        : state(sharedState)
        , session(sharedState.session)
        , trackId(id)
        , recording(sharedState.session, id)
        , automation(sharedState, controller::ChainOwner::track(id))
    {
        recording.addTo(*this);
        automation.addTo(*this);
        inputMeter.setScaleVisible(false);
        inputMeter.setTooltip("Input level, before the fader (click to clear the clip indicators)");
        addAndMakeVisible(inputMeter);

        name.setEditable(false, true, false);
        ThemedLookAndFeel::setFontRole(name, appearance::FontRole::heading);
        name.setTooltip("Double-click to rename");
        name.onTextChange = [this] { session.renameTrack(trackId, name.getText()); };
        name.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(name);

        mute.setTooltip("Mute");
        solo.setTooltip("Solo: only soloed tracks are heard");
        ThemedLookAndFeel::setColourRole(mute, appearance::ColourRole::mute);
        ThemedLookAndFeel::setColourRole(solo, appearance::ColourRole::solo);
        for (auto* button : {&mute, &solo})
        {
            button->setWantsKeyboardFocus(false);
            addAndMakeVisible(*button);
        }

        mute.onClick = [this] { session.setTrackMuted(trackId, !mute.getToggleState()); };
        solo.onClick = [this] { session.setTrackSoloed(trackId, !solo.getToggleState()); };
    }

    [[nodiscard]] model::ObjectId getTrackId() const noexcept { return trackId; }

    void show(const model::Track& track, bool audible)
    {
        if (!name.isBeingEdited())
            name.setText(track.getName(), juce::dontSendNotification);
        mute.setToggleState(track.isMuted(), juce::dontSendNotification);
        solo.setToggleState(track.isSoloed(), juce::dontSendNotification);
        recording.show(track);
        // A MIDI track has no audio input to measure.
        inputMeter.setVisible(!track.isMidi());
        ThemedLookAndFeel::setColourRole(name, audible ? appearance::ColourRole::text
                                                       : appearance::ColourRole::textSecondary);

        // The automation view swaps the recording controls for the automation's.
        const bool automated = state.showAutomation;
        for (auto* control : std::initializer_list<juce::Component*>{&recording.arm, &recording.monitoring,
                                                                     &recording.input})
            control->setVisible(!automated);
        automation.setVisible(automated);
        if (automated)
            automation.show();
        if (automated != laidOutForAutomation)
            resized();
    }

    /** Co-op: who else holds this track, and who looks at it. */
    void showCoop(std::optional<controller::CoopPeer> holder, std::vector<controller::CoopPeer> lookers)
    {
        std::vector<std::pair<int, juce::String>> marks;
        if (holder)
            marks.emplace_back(holder->colour, holder->name);
        for (const auto& peer : lookers)
            marks.emplace_back(peer.colour, peer.name);
        const auto holderColour = holder ? std::optional<int>(holder->colour) : std::nullopt;
        if (marks == coopMarks && holderColour == lockColour)
            return;
        coopMarks = std::move(marks);
        lockColour = holderColour;
        name.setTooltip(holder ? holder->name + " is editing this track: it changes only when they are done."
                               : juce::String("Double-click to rename"));
        resized();
        repaint();
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (coopMarks.empty())
            return;
        const auto& look = ThemedLookAndFeel::get(*this);
        if (lockColour)
        {
            g.setColour(look.participantColour(*lockColour));
            g.fillRect(0, 0, 3, getHeight());
        }
        auto chip = juce::Rectangle<float>(static_cast<float>(name.getRight() + 4), 9.0f, 16.0f, 16.0f);
        for (const auto& [colour, who] : coopMarks)
        {
            drawParticipantChip(g, *this, chip, colour, who);
            chip.translate(chip.getWidth() + 3.0f, 0.0f);
        }
    }

    void updateMeter(double elapsedSeconds)
    {
        if (!inputMeter.isVisible())
            return;
        const auto peaks = session.getTrackInputPeaks(trackId);
        inputMeter.update(peaks[0], peaks[1], elapsedSeconds);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8, 6);
        inputMeter.setBounds(area.removeFromRight(10));
        area.removeFromRight(6);
        // Room for the co-op marks after the name.
        name.setBounds(area.removeFromTop(24).withTrimmedRight(static_cast<int>(coopMarks.size()) * 19));
        area.removeFromTop(6);

        auto row = area.removeFromTop(24);
        const auto place = [&row](juce::Component& component, int width)
        {
            component.setBounds(row.removeFromLeft(width));
            row.removeFromLeft(4);
        };
        place(mute, 28);
        place(solo, 28);
        row.removeFromLeft(4);
        laidOutForAutomation = state.showAutomation;
        automation.place(row);
        place(recording.arm, 28);
        place(recording.monitoring, 42);
        recording.input.setBounds(row);
    }

    void paint(juce::Graphics& g) override
    {
        ThemedLookAndFeel::get(*this).drawTrackHeader(g, getLocalBounds().toFloat());
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem("Rename Track", [this] { name.showEditor(); });
            menu.addItem("Delete Track", [this] {
                // Deleting destroys this header: leave the menu callback first.
                juce::MessageManager::callAsync(
                    [&owner = session, id = trackId] { owner.removeTrack(id); });
            });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
        }
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        name.showEditor();
    }

private:
    ArrangeState& state;
    controller::Session& session;
    const model::ObjectId trackId;
    juce::Label name;
    juce::TextButton mute{"M"};
    juce::TextButton solo{"S"};
    TrackInputControls recording;
    AutomationControls automation;
    bool laidOutForAutomation = false;
    LevelMeter inputMeter;
    std::vector<std::pair<int, juce::String>> coopMarks; // colour and name: the holder first
    std::optional<int> lockColour;
};

TrackHeaderList::TrackHeaderList(ArrangeState& sharedState)
    : state(sharedState)
    , session(sharedState.session)
{
    refresh();
}

TrackHeaderList::~TrackHeaderList() = default;

void TrackHeaderList::refresh()
{
    const auto& project = session.getProject();

    // Rebuild only if the tracks themselves changed, so that an open name editor
    // survives other edits.
    bool sameTracks = static_cast<int>(headers.size()) == project.getNumTracks();
    for (int i = 0; sameTracks && i < project.getNumTracks(); ++i)
        sameTracks =
            headers[static_cast<std::size_t>(i)]->getTrackId() == project.getTrack(i).getId();

    if (!sameTracks)
    {
        headers.clear();
        for (int i = 0; i < project.getNumTracks(); ++i)
        {
            headers.push_back(std::make_unique<Header>(state, project.getTrack(i).getId()));
            addAndMakeVisible(*headers.back());
        }
        resized();
    }

    const auto others = state.inCoop() ? state.coop->getOthers() : std::vector<controller::CoopPeer>{};
    for (int i = 0; i < project.getNumTracks(); ++i)
    {
        auto& header = *headers[static_cast<std::size_t>(i)];
        header.show(project.getTrack(i), project.isTrackAudible(i));
        const auto id = project.getTrack(i).getId();
        const auto holder =
            state.inCoop() ? state.coop->findOtherHolder(model::EditScope::track(id)) : std::nullopt;
        std::vector<controller::CoopPeer> lookers; // apart from the holder, shown already
        for (const auto& peer : others)
            if (peer.connected && peer.presence.track == id && (!holder || holder->number != peer.number))
                lookers.push_back(peer);
        header.showCoop(holder, std::move(lookers));
    }

    // The buses' and the master's rows, in the automation view.
    std::vector<controller::ChainOwner> owners;
    for (int row = project.getNumTracks(); row < state.numRows(); ++row)
        owners.push_back(state.ownerOfRow(row));
    bool sameOwners = owners.size() == ownerHeaders.size();
    for (std::size_t i = 0; sameOwners && i < owners.size(); ++i)
        sameOwners = ownerHeaders[i]->getOwner() == owners[i];
    if (!sameOwners || showingAutomation != state.showAutomation)
    {
        showingAutomation = state.showAutomation;
        ownerHeaders.clear();
        for (const auto& owner : owners)
        {
            ownerHeaders.push_back(std::make_unique<OwnerHeader>(state, owner));
            addAndMakeVisible(*ownerHeaders.back());
        }
        resized();
    }
    for (std::size_t i = 0; i < owners.size(); ++i)
    {
        const auto& owner = owners[i];
        const int bus = owner.kind == controller::ChainOwner::Kind::bus ? project.indexOfBus(owner.id) : -1;
        ownerHeaders[i]->show(bus >= 0 ? project.getBus(bus).getName() : juce::String("Master"));
    }
}

void TrackHeaderList::updateMeters(double elapsedSeconds)
{
    for (auto& header : headers)
        header->updateMeter(elapsedSeconds);
}

void TrackHeaderList::resized()
{
    for (std::size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds(0, static_cast<int>(i) * ArrangeState::laneHeight, getWidth(),
                              ArrangeState::laneHeight);
    for (std::size_t i = 0; i < ownerHeaders.size(); ++i)
        ownerHeaders[i]->setBounds(0, static_cast<int>(headers.size() + i) * ArrangeState::laneHeight,
                                   getWidth(), ArrangeState::laneHeight);
}

void TrackHeaderList::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(),
                                            appearance::ColourRole::window, false);
}

} // namespace daw::ui::desktop
