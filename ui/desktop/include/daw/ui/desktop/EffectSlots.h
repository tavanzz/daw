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
    - Bypassed effects are dimmed; one silenced for producing NaN or infinity is red.
    - The wheel scrolls when there are more effects than rows.
*/
class EffectSlots final : public juce::Component
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

private:
    struct Row
    {
        model::ObjectId id = 0;
        juce::String name;
        bool bypassed = false;
        bool failed = false;
        bool operator==(const Row&) const = default;
    };

    [[nodiscard]] int visibleRows() const noexcept;
    [[nodiscard]] int rowAt(float y) const noexcept;
    void showEffectMenu(const Row& row, int index);
    void showAddMenu();

    controller::Session& session;
    const controller::ChainOwner owner;
    std::vector<Row> rows;
    int scroll = 0;
};

/** The user-facing name of an effect type ("Equaliser"); the type itself if unknown. */
[[nodiscard]] juce::String effectTypeName(const juce::String& type);

} // namespace daw::ui::desktop
