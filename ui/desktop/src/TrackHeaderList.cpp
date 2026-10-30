#include "daw/ui/desktop/TrackHeaderList.h"

#include "daw/ui/desktop/ArrangeState.h"

namespace daw::ui::desktop
{

class TrackHeaderList::Header final : public juce::Component
{
public:
    Header(controller::Session& sessionToEdit, model::ObjectId id)
        : session(sessionToEdit)
        , trackId(id)
    {
        name.setEditable(false, true, false);
        name.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        name.setTooltip("Double-click to rename");
        name.onTextChange = [this] { session.renameTrack(trackId, name.getText()); };
        name.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(name);

        mute.setTooltip("Mute");
        solo.setTooltip("Solo: only soloed tracks are heard");
        mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffc62828));
        solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xfff9a825));
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
        name.setColour(juce::Label::textColourId,
                       audible ? juce::Colours::white : juce::Colours::grey);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8, 6);
        name.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        mute.setBounds(area.removeFromLeft(32).withHeight(24));
        area.removeFromLeft(6);
        solo.setBounds(area.removeFromLeft(32).withHeight(24));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff2b2b2b));
        g.setColour(juce::Colour(0xff1a1a1a));
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
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
    controller::Session& session;
    const model::ObjectId trackId;
    juce::Label name;
    juce::TextButton mute{"M"};
    juce::TextButton solo{"S"};
};

TrackHeaderList::TrackHeaderList(controller::Session& sessionToEdit)
    : session(sessionToEdit)
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
            headers.push_back(std::make_unique<Header>(session, project.getTrack(i).getId()));
            addAndMakeVisible(*headers.back());
        }
        resized();
    }

    for (int i = 0; i < project.getNumTracks(); ++i)
        headers[static_cast<std::size_t>(i)]->show(project.getTrack(i), project.isTrackAudible(i));
}

void TrackHeaderList::resized()
{
    for (std::size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds(0, static_cast<int>(i) * ArrangeState::laneHeight, getWidth(),
                              ArrangeState::laneHeight);
}

void TrackHeaderList::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff232323));
}

} // namespace daw::ui::desktop
