#pragma once

#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace daw::ui::desktop
{

/**
    The insert effects of a track, a bus or the master, in a mixer strip: one row
    per effect, in processing order, and a row to add one.

    - Click an effect to open its editor (onOpen); right-click it to bypass or switch
      it on, move it up or down, remove it, or reactivate it after a failure.
    - A MIDI track's instrument comes first, on a row of its own: click to open its
      editor; right-click to bypass it, change it for another one or remove it; with
      none, "+ Instrument" chooses one.
    - Bypassed effects are dimmed; one silenced for producing NaN or infinity is red;
      a slow plugin is in the warning colour; a plugin that is loading, missing, failed
      or kept from loading is a placeholder ("Load Plugin" in its menu for the last).
    - The add and instrument menus list the catalogue's plugins too, by manufacturer.
    - An effect or instrument dragged from the plugin browser goes in where it is
      dropped (an instrument replaces a MIDI track's instrument).
    - The wheel scrolls when there are more effects than rows.
*/
class EffectSlots final : public juce::Component, public juce::DragAndDropTarget
{
public:
    static constexpr int rowHeight = 18;

    EffectSlots(controller::Session& sessionToEdit, controller::ChainOwner chainOwner);

    /** Opens the editor of an effect. */
    std::function<void(model::ObjectId effectId)> onOpen;

    /** Rereads the effects (after an edit) and their state (failures). */
    void refresh(bool projectChanged);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // juce::DragAndDropTarget: items from the plugin browser.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    /** What a row shows, for tests: its name with the plugin's state ("Gain (missing)"). */
    [[nodiscard]] juce::StringArray getRowTexts() const;

private:
    struct Row
    {
        model::ObjectId id = 0; ///< 0: the instrument row of a MIDI track without one
        juce::String name;
        bool bypassed = false;
        bool failed = false;
        bool instrument = false;
        bool isPlugin = false;
        controller::PluginStatus status = controller::PluginStatus::ready;
        bool slow = false;
        bool operator==(const Row&) const = default;
    };

    [[nodiscard]] static juce::String textOf(const Row& row);
    /** Where in the chain a drop at this height goes (an index of the owner's effects). */
    [[nodiscard]] int chainIndexAt(int y) const noexcept;

    [[nodiscard]] int visibleRows() const noexcept;
    [[nodiscard]] int rowAt(float y) const noexcept;
    void showEffectMenu(const Row& row, int index);
    void showInstrumentMenu(const Row& row);
    void showAddMenu();
    [[nodiscard]] int firstEffectRow() const noexcept;
    void reportProblem();

    controller::Session& session;
    const controller::ChainOwner owner;
    std::vector<Row> rows;
    int scroll = 0;
    int dropRow = -1; ///< where a drag would go, while one is over
    std::vector<juce::PluginDescription> menuPlugins; ///< the plugins of the open menu
};

/** The user-facing name of an effect or instrument type ("Equaliser"); the type
    itself if unknown. */
[[nodiscard]] juce::String effectTypeName(const juce::String& type);

/** The user-facing name of an effect: a plugin's own name, else its type's. */
[[nodiscard]] juce::String effectDisplayName(const model::Effect& effect);

} // namespace daw::ui::desktop
