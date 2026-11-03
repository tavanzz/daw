#pragma once

#include "daw/ui/desktop/AnimationLink.h"
#include "daw/ui/desktop/ArrangeState.h"
#include "daw/ui/desktop/AutomationLanes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace daw::ui::desktop
{

/**
    The tracks' clips on the timeline, with the grid, the loop region and the
    playhead.

    - Click a clip to select it (Shift adds to the selection); drag it to move it,
      also onto another track; drag its left or right edge to trim it. Moves and
      edges snap to the grid unless Cmd is held. Each drag is one undo step.
    - Click empty space to move the playhead there.
    - Drop audio files: one file onto a track goes onto that track, otherwise each
      file gets a new track; all start where they were dropped.
    - In the automation view the clips are dimmed and the mouse edits each row's
      automation lane instead (AutomationLanes); the buses and the master get rows
      of their own.
*/
class TrackLanes final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit TrackLanes(ArrangeState& sharedState);

    void setPlayhead(model::Ticks position, bool isPlaying);

    /** The project, the view or the selection changed: paint the lanes again (a plain
        repaint() puts back the image of the last content). */
    void contentChanged();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    enum class Zone
    {
        body,
        startEdge,
        endEdge
    };

    struct Hit
    {
        model::ObjectId clipId = 0;
        int trackIndex = 0;
        Zone zone = Zone::body;
    };

    struct Drag
    {
        Hit hit;
        model::ClipBounds original;
        model::Ticks pointerStart = 0;
        bool started = false;
    };

    [[nodiscard]] std::optional<Hit> clipAt(juce::Point<float> point) const;
    [[nodiscard]] int trackIndexAt(float y) const;
    [[nodiscard]] juce::Rectangle<float> clipBounds(int trackIndex, const model::Clip& clip) const;
    void drawClip(juce::Graphics& g, int trackIndex, const model::Clip& clip) const;
    void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area, const model::Clip& clip,
                      const engine::io::PooledSource& source) const;
    void drawRecordings(juce::Graphics& g) const;

    [[nodiscard]] juce::Rectangle<float> rowBounds(int row) const;
    [[nodiscard]] juce::Rectangle<int> visibleArea() const;
    void paintContent(juce::Graphics& g, bool translucent) const;
    void repaintPlayhead();
    void updateAnimations();

    ArrangeState& state;
    AutomationLanes automation{state};
    model::Ticks playhead = 0;
    bool playing = false;

    // The image of the lanes' content, for the visible area at the display's scale.
    juce::Image cache;
    juce::Rectangle<int> cacheArea;
    float cacheScale = 0.0f;
    bool cacheTranslucent = false;
    bool contentValid = false;

    AnimationLink backgroundLink{*this, appearance::AnimationEffect::movingBackgrounds,
                                 [this](double) { repaint(visibleArea()); }};
    AnimationLink playheadLink{*this, appearance::AnimationEffect::cursorGlow,
                               [this](double) { repaintPlayhead(); }};
    std::optional<Drag> drag;
    std::optional<juce::Rectangle<float>> dropIndicator;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackLanes)
};

} // namespace daw::ui::desktop
