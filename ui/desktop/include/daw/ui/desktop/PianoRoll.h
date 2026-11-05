#pragma once

#include "daw/controller/NoteEditing.h"
#include "daw/controller/PianoRollViewport.h"
#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>

namespace daw::ui::desktop
{

/**
    The piano roll: one MIDI clip's notes, edited with the mouse, over a keyboard and
    under a ruler, with the velocity lane and controller lanes below.

    Controller lanes (pitch bend, mod wheel, sustain, aftertouch, any CC; the Lanes
    menu, and every type the clip has events for) show the values as steps: click adds
    an event, drag moves it, double-click deletes it, Alt+drag draws a straight line of
    events; right-click clears or hides a lane.

    - Click on empty space adds a note (as long and as loud as the last note touched;
      at first one grid step and velocity 100) on the grid line at or before the
      click; drag on empty space selects with a rectangle (Shift adds).
    - Drag a note to move it (its start snaps, the selection follows; Alt copies),
      its edges to resize it; double-click deletes it. Cmd bypasses snapping.
    - Up and down transpose the selection by a semitone (Shift: an octave); Backspace
      deletes it; Cmd+A selects every note; Cmd+D duplicates it after itself; Q
      quantises it (every note when none is selected) as the toolbar says.
    - The keys and every note being drawn or dragged sound through the track
      (previews).
    - The wheel scrolls the keys (Shift or sideways: the time), Cmd+wheel zooms the
      time, Alt+wheel the keys.

    Every edit is one undo step through the Session; the rules are the controller's
    (NoteEditing.h).
*/
class PianoRoll final : public juce::Component
{
public:
    explicit PianoRoll(controller::Session& sessionToEdit);
    ~PianoRoll() override;

    /** Shows a MIDI clip (0: none); its notes come into view. */
    void setClip(model::ObjectId clipId);
    [[nodiscard]] model::ObjectId getClip() const noexcept;

    /** The project changed: redraw, or show nothing if the clip is gone. */
    void refresh();

    void setPlayhead(model::Ticks position, bool playing);

    /** Commands (also from the menus). */
    void quantise();
    void transpose(int semitones);
    void selectAll();
    void deleteSelection();
    void duplicateSelection();
    [[nodiscard]] bool hasSelection() const noexcept;

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;

    /** Shared by the parts of the piano roll. */
    struct State;

private:
    class Keyboard;
    class NoteGrid;
    class Ruler;
    class VelocityLane;
    class ControllerLane;
    class ControllerLanes;

    void showGridMenu();
    void showQuantiseMenu();
    void showLanesMenu();
    void updateToolbar();
    void showPendingView();

    // What a newly shown clip brings into view, as soon as there is room to show it.
    struct PendingView
    {
        model::Ticks start = 0;
        model::Ticks end = 0;
        int lowKey = 0;
        int highKey = 0;
    };
    std::optional<PendingView> pendingView;

    std::unique_ptr<State> state;
    std::unique_ptr<Keyboard> keyboard;
    std::unique_ptr<NoteGrid> grid;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<VelocityLane> velocities;
    std::unique_ptr<ControllerLanes> controllers;

    juce::Label title;
    juce::TextButton gridButton;
    juce::TextButton quantiseValue;
    juce::Slider strength;
    juce::ToggleButton ends{"Ends"};
    juce::TextButton quantiseButton{"Quantise"};
    juce::TextButton lanesButton{"Lanes"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRoll)
};

} // namespace daw::ui::desktop
