#pragma once

#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace daw::ui::desktop
{

/**
    A track's sends, in its mixer strip: one row per send, the bar showing its level
    on a fader scale, and a row to add one (to a bus, or to a new bus).

    - Drag along a row to set the level (one undo step), double-click for 0 dB;
      right-click for pre-/post-fader or to remove the send.
    - While the level is automated the row follows the automation and cannot be
      dragged.
*/
class SendSlots final : public juce::Component
{
public:
    static constexpr int rowHeight = 18;

    SendSlots(controller::Session& sessionToEdit, model::ObjectId track);

    void refresh(bool projectChanged);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    struct Row
    {
        model::ObjectId id = 0;
        juce::String bus;
        double level = 1.0;
        bool preFader = false;
        bool automated = false;
        bool operator==(const Row&) const = default;
    };

    [[nodiscard]] int rowAt(float y) const noexcept;
    void setLevelFrom(const Row& row, float x);
    void showSendMenu(const Row& row);
    void showAddMenu();

    controller::Session& session;
    const model::ObjectId trackId;
    std::vector<Row> rows;
    int dragging = -1;
};

} // namespace daw::ui::desktop
