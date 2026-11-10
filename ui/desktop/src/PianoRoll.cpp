#include "daw/ui/desktop/PianoRoll.h"

#include "daw/controller/NotePreview.h"
#include "daw/engine/fx/Sampler.h"
#include "daw/ui/desktop/CoopPanels.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace daw::ui::desktop
{

namespace
{

constexpr int toolbarHeight = 30;
constexpr int keyboardWidth = 64;
constexpr int rulerHeight = 20;
constexpr int velocityHeight = 64;
constexpr float edgePixels = 5.0f;

bool isBlackKey(int key)
{
    switch (key % 12)
    {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

// Quantise values offered (the grid's divisions, not automatic).
constexpr std::array quantiseDivisions{
    model::GridDivision::quarter,        model::GridDivision::eighth,
    model::GridDivision::sixteenth,      model::GridDivision::thirtySecond,
    model::GridDivision::sixtyFourth,    model::GridDivision::quarterTriplet,
    model::GridDivision::eighthTriplet,  model::GridDivision::sixteenthTriplet,
    model::GridDivision::thirtySecondTriplet};

// A controller's name in the lanes and menus.
juce::String controllerName(const model::ControllerType& type)
{
    using Kind = model::ControllerType::Kind;
    if (type.kind == Kind::pitchBend)
        return "Pitch Bend";
    if (type.kind == Kind::channelPressure)
        return "Aftertouch";
    switch (type.number)
    {
    case 1:
        return "Mod Wheel (CC1)";
    case 2:
        return "Breath (CC2)";
    case 7:
        return "Volume (CC7)";
    case 10:
        return "Pan (CC10)";
    case 11:
        return "Expression (CC11)";
    case 64:
        return "Sustain (CC64)";
    default:
        return "CC" + juce::String(type.number);
    }
}

} // namespace

struct PianoRoll::State
{
    explicit State(controller::Session& sessionToEdit)
        : session(sessionToEdit)
    {
        viewport.timeline.setTicksPerPixel(static_cast<double>(model::ticksPerSecond) / 200.0);
    }

    controller::Session& session;
    model::ObjectId clipId = 0;
    controller::PianoRollViewport viewport;
    controller::NoteIds selection;
    model::GridDivision division = model::GridDivision::automatic;
    controller::QuantiseSettings quantise;
    model::Ticks newLength = 0; // 0: one grid step
    int newVelocity = model::Note::defaultVelocity;
    model::Ticks playhead = 0;
    std::function<void()> changed; // repaint every part

    // Co-op: the others' pointers and selections in this clip; this one's pointer.
    const controller::CoopSession* coop = nullptr;
    int pointerKey = -1;
    model::Ticks pointerPosition = 0; // on the timeline

    [[nodiscard]] std::optional<model::MidiClip> clip() const { return session.findMidiClip(clipId); }

    [[nodiscard]] int trackIndex() const
    {
        const auto location = session.getProject().findClip(clipId);
        return location ? location->trackIndex : -1;
    }

    [[nodiscard]] model::ObjectId trackId() const
    {
        const int index = trackIndex();
        return index >= 0 ? session.getProject().getTrack(index).getId() : 0;
    }

    [[nodiscard]] controller::ClipFrame frame() const
    {
        const auto midi = clip();
        return midi ? controller::ClipFrame::of(midi->getClip()) : controller::ClipFrame{};
    }

    [[nodiscard]] std::vector<model::Note> notes() const
    {
        const auto midi = clip();
        return midi ? midi->getNotes() : std::vector<model::Note>{};
    }

    [[nodiscard]] std::vector<model::ControllerEvent> controllerEvents(const model::ControllerType& type) const
    {
        const auto midi = clip();
        return midi ? midi->getControllerEvents(type) : std::vector<model::ControllerEvent>{};
    }

    [[nodiscard]] model::GridDivision gridNow() const
    {
        return viewport.timeline.resolveGrid(division, session.getProject().getTempoMap());
    }

    [[nodiscard]] controller::Snapper snapper(bool bypass) const
    {
        if (bypass || !session.getProject().isSnapEnabled())
            return {};
        return controller::Snapper(model::Grid(session.getProject().getTempoMap(), gridNow()));
    }

    [[nodiscard]] model::Ticks stepLength() const
    {
        return static_cast<model::Ticks>(
            std::llround(model::Grid::divisionTicks(session.getProject().getTempoMap(), gridNow())));
    }

    void setNotes(std::vector<model::Note> notes, const juce::String& action)
    {
        session.setNotes(clipId, std::move(notes), action);
        if (changed)
            changed();
    }

    // Previews: one key at a time from the mouse, ended where it was started.
    void startPreview(int key, int velocity)
    {
        if (const auto track = trackId(); track != 0)
            preview.play(track, key, velocity);
        else
            preview.stop();
    }

    void stopPreview() { preview.stop(); }

    [[nodiscard]] int previewKey() const noexcept { return preview.getKey(); }

    controller::NotePreview preview{session};
};

// The keys, the highest at the top; a click plays the key.
class PianoRoll::Keyboard final : public juce::Component
{
public:
    explicit Keyboard(State& shared)
        : state(shared)
    {
    }

    void paint(juce::Graphics& g) override
    {
        const auto& look = ThemedLookAndFeel::get(*this);
        auto& viewport = state.viewport;
        const int top = viewport.yToKey(0.0);
        const int bottom = viewport.yToKey(static_cast<double>(getHeight()));
        for (int key = bottom; key <= top; ++key)
        {
            const auto y = static_cast<float>(viewport.keyToY(key));
            const juce::Rectangle<float> area(0.0f, y, static_cast<float>(getWidth()),
                                              static_cast<float>(viewport.getKeyHeight()));
            look.drawPianoKey(g, area, isBlackKey(key), key % 12 == 0 ? engine::fx::keyName(key) : juce::String(),
                              key == state.previewKey());
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        state.startPreview(state.viewport.yToKey(event.position.y), state.newVelocity);
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        const int key = state.viewport.yToKey(event.position.y);
        if (key != state.previewKey())
        {
            state.startPreview(key, state.newVelocity);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        state.stopPreview();
        repaint();
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (event.mods.isAltDown())
            state.viewport.zoomAround(std::pow(2.0, wheel.deltaY), event.position.y);
        else
            state.viewport.scrollByPixels(wheel.deltaY * 200.0);
        if (state.changed)
            state.changed();
    }

private:
    State& state;
};

// Bars and beats over the notes; a click moves the playhead.
class PianoRoll::Ruler final : public juce::Component
{
public:
    explicit Ruler(State& shared)
        : state(shared)
    {
    }

    void paint(juce::Graphics& g) override
    {
        const auto& look = ThemedLookAndFeel::get(*this);
        const auto area = getLocalBounds().toFloat();
        look.drawRulerBackground(g, area);
        const auto tempoMap = state.session.getProject().getTempoMap();
        for (const auto& mark : controller::barRulerMarks(state.viewport.timeline, tempoMap))
        {
            const auto x = static_cast<float>(state.viewport.timeline.ticksToX(mark.position));
            look.drawRulerMark(g, x, mark.major ? 0.0f : area.getHeight() * 0.6f, area.getHeight(),
                               mark.major);
            if (mark.label.isNotEmpty())
                look.drawRulerLabel(g, mark.label, {x + 3.0f, 0.0f, 60.0f, area.getHeight()}, mark.major);
        }
        const auto frame = state.frame();
        if (state.clipId != 0)
            look.drawRulerLoop(g, static_cast<float>(state.viewport.timeline.ticksToX(frame.start)),
                               static_cast<float>(state.viewport.timeline.ticksToX(frame.end)),
                               3.0f, true);
        look.drawPlayheadMarker(g, static_cast<float>(state.viewport.timeline.ticksToX(state.playhead)),
                                area.getHeight());
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        state.session.seek(std::max(model::Ticks{0}, state.viewport.timeline.xToTicks(event.position.x)));
    }

private:
    State& state;
};

// The notes on their rows, with the grid, the clip's window and the playhead.
class PianoRoll::NoteGrid final : public juce::Component
{
public:
    explicit NoteGrid(State& shared)
        : state(shared)
    {
        setWantsKeyboardFocus(false);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& look = ThemedLookAndFeel::get(*this);
        auto& viewport = state.viewport;
        const auto width = static_cast<float>(getWidth());
        const auto height = static_cast<float>(getHeight());

        // Rows: black keys darker.
        const int top = viewport.yToKey(0.0);
        const int bottom = viewport.yToKey(static_cast<double>(getHeight()));
        for (int key = bottom; key <= top; ++key)
            look.drawNoteRow(g, {0.0f, static_cast<float>(viewport.keyToY(key)), width,
                                 static_cast<float>(viewport.getKeyHeight())},
                             isBlackKey(key), key % 12 == 0);

        // The grid, bar lines brighter.
        const auto tempoMap = state.session.getProject().getTempoMap();
        const model::Grid lines(tempoMap, state.gridNow());
        const auto start = viewport.timeline.getStart();
        const auto end = viewport.timeline.getEnd();
        if (static_cast<double>(end - start) / model::Grid::divisionTicks(tempoMap, state.gridNow()) <
            width / 3.0)
            for (const auto line : lines.linesBetween(start, end + 1))
                look.drawGridLine(g, static_cast<float>(viewport.timeline.ticksToX(line)), 0.0f, height,
                                  tempoMap.barStart(tempoMap.barAt(line)) == line);

        if (state.clipId == 0)
        {
            g.setColour(look.colour(appearance::ColourRole::textSecondary));
            g.setFont(look.font(appearance::FontRole::label));
            g.drawText("Double-click a MIDI clip to edit its notes here", getLocalBounds(),
                       juce::Justification::centred);
            return;
        }

        // Outside the clip's window, shaded: notes there are not heard.
        const auto frame = state.frame();
        const auto x0 = static_cast<float>(viewport.timeline.ticksToX(frame.start));
        const auto x1 = static_cast<float>(viewport.timeline.ticksToX(frame.end));
        g.setColour(look.colour(appearance::ColourRole::shadow));
        g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, std::max(0.0f, x0), height));
        g.fillRect(juce::Rectangle<float>(x1, 0.0f, std::max(0.0f, width - x1), height));

        const auto colour = look.clipStyle(std::max(0, state.trackIndex()),
                                           ThemedLookAndFeel::ClipState::ready, 0)
                                .waveform;
        for (const auto& note : state.notes())
            look.drawNote(g, noteBounds(note, frame), colour, note.velocity,
                          state.selection.contains(note.id));

        if (band)
            look.drawSelectionBand(g, *band);

        if (state.coop != nullptr && state.coop->isActive())
            paintOthers(g, frame);

        look.drawPlayhead(g, static_cast<float>(viewport.timeline.ticksToX(state.playhead)), 0.0f,
                          height, {});
    }

    // Others with this clip open: the notes they selected and their pointer, in their
    // colours.
    void paintOthers(juce::Graphics& g, const controller::ClipFrame& frame) const
    {
        const auto& look = ThemedLookAndFeel::get(*this);
        const auto notes = state.notes();
        for (const auto& peer : state.coop->getOthers())
        {
            if (!peer.connected || peer.presence.clip != state.clipId)
                continue;
            const auto colour = look.participantColour(peer.colour);
            g.setColour(colour);
            for (const auto& note : notes)
                if (std::find(peer.presence.selectedNotes.begin(), peer.presence.selectedNotes.end(), note.id)
                    != peer.presence.selectedNotes.end())
                    g.drawRect(noteBounds(note, frame).expanded(1.0f), 2.0f);
            if (peer.presence.cursorKey >= 0)
            {
                const auto x = static_cast<float>(state.viewport.timeline.ticksToX(peer.presence.clipCursorPosition));
                const auto y = static_cast<float>(state.viewport.keyToY(peer.presence.cursorKey));
                const auto keyHeight = static_cast<float>(state.viewport.getKeyHeight());
                g.setColour(colour);
                g.fillRect(juce::Rectangle<float>(x - 1.0f, y, 2.0f, keyHeight));
                const auto chip = std::max(12.0f, std::min(16.0f, keyHeight));
                drawParticipantChip(g, *this, {x + 3.0f, y + (keyHeight - chip) / 2.0f, chip, chip}, peer.colour,
                                    peer.name);
            }
        }
    }

    void notePointer(juce::Point<float> position)
    {
        state.pointerKey = state.viewport.yToKey(position.y);
        state.pointerPosition = state.viewport.timeline.xToTicks(position.x);
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        state.pointerKey = -1;
    }

    [[nodiscard]] juce::Rectangle<float> noteBounds(const model::Note& note,
                                                    const controller::ClipFrame& frame) const
    {
        const auto& viewport = state.viewport;
        const auto x0 = static_cast<float>(viewport.timeline.ticksToX(frame.toTimeline(note.start)));
        const auto x1 = static_cast<float>(viewport.timeline.ticksToX(frame.toTimeline(note.getEnd())));
        return {x0, static_cast<float>(viewport.keyToY(note.key)), std::max(2.0f, x1 - x0),
                static_cast<float>(viewport.getKeyHeight())};
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        notePointer(event.position);
        const auto hit = hitAt(event.position);
        setMouseCursor(hit.part == controller::NotePart::start || hit.part == controller::NotePart::end
                           ? juce::MouseCursor::LeftRightResizeCursor
                       : hit.part == controller::NotePart::body ? juce::MouseCursor::DraggingHandCursor
                                                                : juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (state.clipId == 0)
            return;
        if (auto* roll = findParentComponentOfClass<PianoRoll>())
            roll->grabKeyboardFocus();

        const auto hit = hitAt(event.position);
        drag = Drag{};
        drag->notes = state.notes();
        drag->pointerStart = state.viewport.timeline.xToTicks(event.position.x);
        drag->keyStart = state.viewport.yToKey(event.position.y);

        if (hit.part == controller::NotePart::none)
        {
            drag->kind = DragKind::empty;
            if (!event.mods.isShiftDown())
                state.selection.clear();
            drag->selectionBefore = state.selection;
            // A click here adds a note: it sounds while the button is down.
            state.startPreview(drag->keyStart, state.newVelocity);
            changed();
            return;
        }

        if (event.mods.isShiftDown())
        {
            if (!state.selection.erase(hit.id))
                state.selection.insert(hit.id);
        }
        else if (!state.selection.contains(hit.id))
        {
            state.selection = {hit.id};
        }

        drag->kind = hit.part == controller::NotePart::start ? DragKind::start
                     : hit.part == controller::NotePart::end ? DragKind::end
                                                              : DragKind::move;
        drag->grabbed = hit.id;
        for (const auto& note : drag->notes)
            if (note.id == hit.id)
            {
                state.newLength = note.length;
                state.newVelocity = note.velocity;
                state.startPreview(note.key, note.velocity);
            }
        changed();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        notePointer(event.position);
        if (!drag)
            return;
        if (!drag->started)
        {
            if (event.getDistanceFromDragStart() < 3)
                return;
            drag->started = true;
            if (drag->kind == DragKind::empty)
                state.stopPreview(); // a rectangle, not a note
            else
                state.session.beginGesture(drag->kind == DragKind::move
                                               ? (event.mods.isAltDown() ? "Copy Notes" : "Move Notes")
                                               : "Resize Notes");
        }

        const auto frame = state.frame();
        const auto delta = state.viewport.timeline.xToTicks(event.position.x) - drag->pointerStart;
        const auto snap = state.snapper(event.mods.isCommandDown());

        switch (drag->kind)
        {
        case DragKind::empty:
        {
            band = juce::Rectangle<float>(event.mouseDownPosition, event.position);
            const auto from = state.viewport.timeline.xToTicks(band->getX());
            const auto to = state.viewport.timeline.xToTicks(band->getRight());
            const int high = state.viewport.yToKey(band->getY());
            const int low = state.viewport.yToKey(band->getBottom());
            state.selection = drag->selectionBefore;
            for (const auto id : controller::notesIn(drag->notes, frame, from, to, low, high))
                state.selection.insert(id);
            changed();
            return;
        }
        case DragKind::move:
        {
            const int keyDelta = state.viewport.yToKey(event.position.y) - drag->keyStart;
            const auto result = controller::movedNotes(drag->notes, state.selection, drag->grabbed, delta,
                                                       keyDelta, frame, snap, event.mods.isAltDown());
            state.selection = result.selection;
            state.setNotes(result.notes, event.mods.isAltDown() ? "Copy Notes" : "Move Notes");
            // The note being dragged sounds at its new key.
            for (const auto& note : result.notes)
                if ((note.id == drag->grabbed || (event.mods.isAltDown() && result.selection.contains(note.id))) &&
                    note.key != state.previewKey())
                {
                    state.startPreview(note.key, note.velocity);
                    break;
                }
            return;
        }
        case DragKind::start:
            state.setNotes(controller::resizedStarts(drag->notes, state.selection, drag->grabbed, delta, frame,
                                                     snap),
                           "Resize Notes");
            return;
        case DragKind::end:
            state.setNotes(controller::resizedEnds(drag->notes, state.selection, drag->grabbed, delta, frame,
                                                   snap),
                           "Resize Notes");
            return;
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        state.stopPreview();
        if (!drag)
            return;

        // A click on empty space adds a note.
        if (drag->kind == DragKind::empty && !drag->started && !event.mods.isShiftDown())
        {
            const auto length = state.newLength > 0 ? state.newLength : state.stepLength();
            auto note = controller::newNoteAt(state.viewport.timeline.xToTicks(event.position.x),
                                              state.viewport.yToKey(event.position.y), length,
                                              state.newVelocity, state.frame(),
                                              state.snapper(event.mods.isCommandDown()));
            const auto id = state.session.addNote(state.clipId, note);
            if (id != 0)
                state.selection = {id};
        }

        if (drag->started && drag->kind != DragKind::empty)
            state.session.endGesture();
        drag.reset();
        band.reset();
        changed();
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const auto hit = hitAt(event.position);
        if (hit.part == controller::NotePart::none)
            return;
        state.selection.erase(hit.id);
        state.session.removeNotes(state.clipId, {hit.id});
        changed();
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        auto& viewport = state.viewport;
        if (event.mods.isCommandDown())
            viewport.timeline.zoomAround(std::pow(2.0, wheel.deltaY * 2.0), event.position.x);
        else if (event.mods.isAltDown())
            viewport.zoomAround(std::pow(2.0, wheel.deltaY), event.position.y);
        else if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) || event.mods.isShiftDown())
            viewport.timeline.scrollByPixels(
                -(std::abs(wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY) * 400.0);
        else
            viewport.scrollByPixels(wheel.deltaY * 200.0);
        changed();
    }

    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override
    {
        state.viewport.timeline.zoomAround(scaleFactor, event.position.x);
        changed();
    }

private:
    enum class DragKind
    {
        empty,
        move,
        start,
        end
    };

    struct Drag
    {
        DragKind kind = DragKind::empty;
        std::vector<model::Note> notes; // as they were when the drag started
        controller::NoteIds selectionBefore;
        std::int32_t grabbed = 0;
        model::Ticks pointerStart = 0;
        int keyStart = 0;
        bool started = false;
    };

    [[nodiscard]] controller::NoteHit hitAt(juce::Point<float> point) const
    {
        if (state.clipId == 0)
            return {};
        const auto edge = static_cast<model::Ticks>(edgePixels * state.viewport.timeline.getTicksPerPixel());
        return controller::hitNote(state.notes(), state.frame(), state.viewport.timeline.xToTicks(point.x),
                                   state.viewport.yToKey(point.y), edge);
    }

    void changed()
    {
        if (state.changed)
            state.changed();
    }

    State& state;
    std::optional<Drag> drag;
    std::optional<juce::Rectangle<float>> band;
};

// A bar per note, as tall as its velocity: drag one to change it, Alt+drag draws a line.
class PianoRoll::VelocityLane final : public juce::Component
{
public:
    explicit VelocityLane(State& shared)
        : state(shared)
    {
    }

    void paint(juce::Graphics& g) override
    {
        const auto& look = ThemedLookAndFeel::get(*this);
        look.drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::lane, false);
        look.drawLaneLabel(g, getLocalBounds().toFloat().removeFromLeft(80.0f).reduced(4.0f, 2.0f),
                           "Velocity");
        if (state.clipId == 0)
            return;

        const auto frame = state.frame();
        const auto colour = look.clipStyle(std::max(0, state.trackIndex()),
                                           ThemedLookAndFeel::ClipState::ready, 0)
                                .waveform;
        for (const auto& note : state.notes())
        {
            const auto x = static_cast<float>(state.viewport.timeline.ticksToX(frame.toTimeline(note.start)));
            look.drawVelocityBar(g, barArea(x, note.velocity), colour, state.selection.contains(note.id));
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (state.clipId == 0)
            return;
        original = state.notes();
        target = noteAt(event.position.x);
        state.session.beginGesture("Velocity");
        mouseDrag(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (state.clipId == 0)
            return;
        const auto frame = state.frame();
        const int velocity = velocityAt(event.position.y);
        if (event.mods.isAltDown())
        {
            const auto from = state.viewport.timeline.xToTicks(event.mouseDownPosition.x);
            const auto to = state.viewport.timeline.xToTicks(event.position.x);
            state.setNotes(controller::velocitiesAlong(original, state.selection, frame, from,
                                                       velocityAt(event.mouseDownPosition.y), to, velocity),
                           "Velocity");
            return;
        }
        if (target == 0)
            return;
        // The note grabbed, and the selection with it if it is selected.
        controller::NoteIds which = state.selection.contains(target) ? state.selection
                                                                     : controller::NoteIds{target};
        state.setNotes(controller::withVelocity(original, which, velocity), "Velocity");
        state.newVelocity = velocity;
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        state.session.endGesture();
        target = 0;
    }

private:
    [[nodiscard]] juce::Rectangle<float> barArea(float x, int velocity) const
    {
        const auto area = getLocalBounds().toFloat().reduced(0.0f, 4.0f);
        const float height = area.getHeight() * static_cast<float>(velocity) / 127.0f;
        return {x - 2.0f, area.getBottom() - height, 5.0f, height};
    }

    [[nodiscard]] int velocityAt(float y) const
    {
        const auto area = getLocalBounds().toFloat().reduced(0.0f, 4.0f);
        return std::clamp(static_cast<int>(std::lround((area.getBottom() - y) / area.getHeight() * 127.0f)),
                          model::Note::minimumVelocity, model::Note::maximumVelocity);
    }

    [[nodiscard]] std::int32_t noteAt(float x) const
    {
        const auto frame = state.frame();
        std::int32_t best = 0;
        float bestDistance = 6.0f;
        for (const auto& note : state.notes())
        {
            const auto distance = std::abs(
                static_cast<float>(state.viewport.timeline.ticksToX(frame.toTimeline(note.start))) - x);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = note.id;
            }
        }
        return best;
    }

    State& state;
    std::vector<model::Note> original;
    std::int32_t target = 0;
};

// One controller's events as steps over the clip: click adds an event (snapped),
// drag moves it, double-click deletes it, Alt+drag draws a straight line of events;
// right-click for the lane's menu.
class PianoRoll::ControllerLane final : public juce::Component
{
public:
    ControllerLane(State& shared, model::ControllerType laneType, std::function<void()> removeLane)
        : state(shared)
        , type(laneType)
        , onRemove(std::move(removeLane))
    {
    }

    [[nodiscard]] const model::ControllerType& getType() const noexcept { return type; }

    void paint(juce::Graphics& g) override
    {
        const auto& look = ThemedLookAndFeel::get(*this);
        look.drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::lane, false);
        if (state.clipId != 0)
        {
            const auto frame = state.frame();
            const auto events = state.controllerEvents(type);
            const auto x0 = static_cast<float>(state.viewport.timeline.ticksToX(frame.start));
            const auto x1 = static_cast<float>(state.viewport.timeline.ticksToX(frame.end));

            // Steps over the visible part of the clip: the value chased at its start,
            // then each event inside it.
            juce::Path steps;
            float y = valueToY(controller::controllerValueAt(events, type, frame, frame.start));
            steps.startNewSubPath(x0, y);
            for (const auto& event : events)
            {
                const auto position = frame.toTimeline(event.position);
                if (position <= frame.start || position >= frame.end)
                    continue;
                const auto x = static_cast<float>(state.viewport.timeline.ticksToX(position));
                steps.lineTo(x, y);
                y = valueToY(event.value);
                steps.lineTo(x, y);
            }
            steps.lineTo(x1, y);
            look.drawControllerSteps(g, steps);

            for (int i = 0; i < static_cast<int>(events.size()); ++i)
            {
                const auto& event = events[static_cast<std::size_t>(i)];
                const auto position = frame.toTimeline(event.position);
                if (position < frame.start || position >= frame.end)
                    continue;
                look.drawControllerEvent(
                    g, {static_cast<float>(state.viewport.timeline.ticksToX(position)), valueToY(event.value)},
                    i == dragged);
            }
        }
        look.drawLaneLabel(g, getLocalBounds().toFloat().removeFromLeft(120.0f).reduced(4.0f, 2.0f),
                           controllerName(type));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        if (state.clipId == 0)
            return;
        original = state.controllerEvents(type);
        dragged = hitAt(event.position);
        state.session.beginGesture("Controller");
        if (event.mods.isAltDown())
            return; // a line, drawn as the mouse moves
        if (dragged < 0)
        {
            // A new event where the click is, which the drag then moves.
            const auto frame = state.frame();
            const auto position = std::clamp(
                state.snapper(event.mods.isCommandDown())(state.viewport.timeline.xToTicks(event.position.x)),
                frame.start, frame.end - 1);
            original.push_back({type, frame.toContent(position), yToValue(event.position.y)});
            dragged = static_cast<int>(original.size()) - 1;
            setEvents(original);
            // Normalising sorted them: find the new one again.
            original = state.controllerEvents(type);
            dragged = hitAt(event.position);
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (state.clipId == 0 || event.mods.isPopupMenu())
            return;
        const auto frame = state.frame();
        if (event.mods.isAltDown())
        {
            const auto from = state.viewport.timeline.xToTicks(event.mouseDownPosition.x);
            const auto to = state.viewport.timeline.xToTicks(event.position.x);
            // An event every four pixels at most, never closer than 1/64 note.
            const auto spacing = std::max<model::Ticks>(
                static_cast<model::Ticks>(std::llround(4.0 * state.viewport.timeline.getTicksPerPixel())),
                static_cast<model::Ticks>(model::Grid::divisionTicks(state.session.getProject().getTempoMap(),
                                                                     model::GridDivision::sixtyFourth)));
            setEvents(controller::eventsAlongLine(original, type, frame, std::min(from, to),
                                                  from <= to ? yToValue(event.mouseDownPosition.y)
                                                             : yToValue(event.position.y),
                                                  std::max(from, to),
                                                  from <= to ? yToValue(event.position.y)
                                                             : yToValue(event.mouseDownPosition.y),
                                                  spacing));
            return;
        }
        if (dragged < 0 || dragged >= static_cast<int>(original.size()))
            return;
        auto events = original;
        auto& moving = events[static_cast<std::size_t>(dragged)];
        const auto start = frame.toTimeline(original[static_cast<std::size_t>(dragged)].position);
        const auto delta = state.viewport.timeline.xToTicks(event.position.x) -
                           state.viewport.timeline.xToTicks(event.mouseDownPosition.x);
        const auto position =
            std::clamp(controller::movedPosition(start, delta, state.snapper(event.mods.isCommandDown())),
                       frame.start, frame.end - 1);
        moving.position = frame.toContent(position);
        moving.value = yToValue(event.position.y);
        setEvents(events);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        state.session.endGesture();
        dragged = -1;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (state.clipId == 0)
            return;
        auto events = state.controllerEvents(type);
        if (const int index = hitAt(event.position); index >= 0)
        {
            events.erase(events.begin() + index);
            setEvents(events, "Delete Controller Event");
        }
    }

private:
    void setEvents(std::vector<model::ControllerEvent> events, const juce::String& action = "Controller")
    {
        state.session.setControllerEvents(state.clipId, type, std::move(events), action);
        if (state.changed)
            state.changed();
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        const bool hasEvents = state.clipId != 0 && !state.controllerEvents(type).empty();
        menu.addItem("Clear " + controllerName(type), hasEvents,  false, [this] {
            setEvents({}, "Clear " + controllerName(type));
        });
        menu.addItem("Hide Lane", [this] {
            // Hiding destroys this lane: leave the menu callback first.
            juce::MessageManager::callAsync([remove = onRemove] { remove(); });
        });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

    [[nodiscard]] juce::Rectangle<float> valueArea() const
    {
        return getLocalBounds().toFloat().reduced(0.0f, 4.0f);
    }

    [[nodiscard]] float valueToY(int value) const
    {
        const auto area = valueArea();
        return area.getBottom() - area.getHeight() * static_cast<float>(value) /
                                      static_cast<float>(type.maximumValue());
    }

    [[nodiscard]] int yToValue(float y) const
    {
        const auto area = valueArea();
        return std::clamp(static_cast<int>(std::lround((area.getBottom() - y) / area.getHeight() *
                                                       static_cast<float>(type.maximumValue()))),
                          0, type.maximumValue());
    }

    [[nodiscard]] int hitAt(juce::Point<float> point) const
    {
        const auto frame = state.frame();
        const auto tickTolerance = static_cast<model::Ticks>(
            std::llround(edgePixels * state.viewport.timeline.getTicksPerPixel()));
        const int valueTolerance = std::max(
            1, static_cast<int>(std::lround(6.0f / valueArea().getHeight() *
                                            static_cast<float>(type.maximumValue()))));
        return controller::hitControllerEvent(state.controllerEvents(type), frame,
                                              state.viewport.timeline.xToTicks(point.x), yToValue(point.y),
                                              tickTolerance, valueTolerance);
    }

    State& state;
    const model::ControllerType type;
    std::function<void()> onRemove;
    std::vector<model::ControllerEvent> original;
    int dragged = -1;
};

// The controller lanes shown: those the clip has events for, and those added from the
// Lanes menu (which stay from clip to clip until hidden).
class PianoRoll::ControllerLanes final : public juce::Component
{
public:
    explicit ControllerLanes(State& shared)
        : state(shared)
    {
    }

    std::function<void()> onLayoutChanged;

    [[nodiscard]] int preferredHeight() const { return laneHeight * static_cast<int>(lanes.size()); }

    [[nodiscard]] bool isShown(const model::ControllerType& type) const
    {
        return std::any_of(lanes.begin(), lanes.end(),
                           [&type](const auto& lane) { return lane->getType() == type; });
    }

    /** A new clip: its lanes and the chosen ones. */
    void reset()
    {
        lanes.clear();
        update();
    }

    /** Lanes for types the clip has gained (recorded, undone), in order. */
    void update()
    {
        auto wanted = chosen;
        if (const auto clip = state.clip())
            for (const auto& type : clip->getControllerTypes())
                if (std::find(wanted.begin(), wanted.end(), type) == wanted.end())
                    wanted.push_back(type);
        std::sort(wanted.begin(), wanted.end());

        bool added = false;
        for (const auto& type : wanted)
            if (!isShown(type))
            {
                lanes.push_back(std::make_unique<ControllerLane>(state, type, [this, type] { hide(type); }));
                addAndMakeVisible(*lanes.back());
                added = true;
            }
        if (added)
        {
            std::sort(lanes.begin(), lanes.end(),
                      [](const auto& a, const auto& b) { return a->getType() < b->getType(); });
            layoutChanged();
        }
    }

    void show(const model::ControllerType& type)
    {
        if (std::find(chosen.begin(), chosen.end(), type) == chosen.end())
            chosen.push_back(type);
        update();
    }

    void hide(const model::ControllerType& type)
    {
        chosen.erase(std::remove(chosen.begin(), chosen.end(), type), chosen.end());
        lanes.erase(std::remove_if(lanes.begin(), lanes.end(),
                                   [&type](const auto& lane) { return lane->getType() == type; }),
                    lanes.end());
        layoutChanged();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        for (auto& lane : lanes)
            lane->setBounds(area.removeFromTop(laneHeight));
    }

private:
    static constexpr int laneHeight = 56;

    void layoutChanged()
    {
        resized();
        if (onLayoutChanged)
            onLayoutChanged();
    }

    State& state;
    std::vector<model::ControllerType> chosen;
    std::vector<std::unique_ptr<ControllerLane>> lanes;
};

PianoRoll::PianoRoll(controller::Session& sessionToEdit)
    : state(std::make_unique<State>(sessionToEdit))
    , keyboard(std::make_unique<Keyboard>(*state))
    , grid(std::make_unique<NoteGrid>(*state))
    , ruler(std::make_unique<Ruler>(*state))
    , velocities(std::make_unique<VelocityLane>(*state))
    , controllers(std::make_unique<ControllerLanes>(*state))
{
    setWantsKeyboardFocus(true);
    state->changed = [this] { repaint(); };
    controllers->onLayoutChanged = [this] { resized(); };
    for (auto* part : std::initializer_list<juce::Component*>{keyboard.get(), grid.get(), ruler.get(),
                                                             velocities.get(), controllers.get()})
        addAndMakeVisible(*part);

    ThemedLookAndFeel::setFontRole(title, appearance::FontRole::heading);
    addAndMakeVisible(title);

    gridButton.setTooltip("The grid notes snap to (N switches snapping)");
    gridButton.onClick = [this] { showGridMenu(); };
    quantiseValue.setTooltip("What Quantise moves the notes to");
    quantiseValue.onClick = [this] { showQuantiseMenu(); };
    strength.setSliderStyle(juce::Slider::LinearHorizontal);
    strength.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
    strength.setRange(0.0, 100.0, 1.0);
    strength.setTextValueSuffix(" %");
    strength.setValue(100.0, juce::dontSendNotification);
    strength.setTooltip("How far Quantise moves the notes towards the grid");
    strength.onValueChange = [this] { state->quantise.strength = strength.getValue() / 100.0; };
    ends.setTooltip("Quantise the notes' ends too");
    ends.onClick = [this] { state->quantise.ends = ends.getToggleState(); };
    quantiseButton.setTooltip("Quantise the selected notes, or every note (Q)");
    quantiseButton.onClick = [this] { quantise(); };
    lanesButton.setTooltip("Shows a controller lane under the notes");
    lanesButton.onClick = [this] { showLanesMenu(); };
    for (auto* control : std::initializer_list<juce::Component*>{&gridButton, &quantiseValue, &strength,
                                                                &ends, &quantiseButton, &lanesButton})
    {
        control->setWantsKeyboardFocus(false);
        addAndMakeVisible(*control);
    }
    updateToolbar();
}

PianoRoll::~PianoRoll()
{
    state->stopPreview();
}

void PianoRoll::setClip(model::ObjectId clipId)
{
    state->stopPreview();
    state->clipId = clipId;
    state->selection.clear();
    state->newLength = 0;

    if (const auto clip = state->clip())
    {
        // Its whole window, and its notes' keys (or the middle of the keyboard), once
        // the piano roll has its size.
        const auto frame = state->frame();
        const auto margin = (frame.end - frame.start) / 16;
        int low = 127;
        int high = 0;
        for (const auto& note : clip->getNotes())
        {
            low = std::min(low, note.key);
            high = std::max(high, note.key);
        }
        if (low > high)
        {
            low = 48;
            high = 72;
        }
        pendingView = PendingView{std::max(model::Ticks{0}, frame.start - margin), frame.end + margin, low, high};
        showPendingView();
    }
    controllers->reset();
    updateToolbar();
    repaint();
}

model::ObjectId PianoRoll::getClip() const noexcept
{
    return state->clipId;
}

void PianoRoll::refresh()
{
    if (state->clipId != 0 && !state->clip())
        setClip(0);
    // The clip moved to another track (or went): its preview ends where it started.
    if (state->preview.isPlaying() && state->preview.getTrack() != state->trackId())
        state->stopPreview();
    controllers->update();
    updateToolbar();
    repaint();
}

void PianoRoll::setPlayhead(model::Ticks position, bool)
{
    if (position == state->playhead)
        return;
    state->playhead = position;
    grid->repaint();
    ruler->repaint();
}

void PianoRoll::updateToolbar()
{
    const auto clip = state->clip();
    title.setText(clip ? clip->getClip().getName() : juce::String("Piano Roll"), juce::dontSendNotification);
    gridButton.setButtonText("Grid " + (state->division == model::GridDivision::automatic
                                            ? juce::String("Auto")
                                            : model::toString(state->division)));
    quantiseValue.setButtonText("To " + model::toString(state->quantise.division));
}

void PianoRoll::showGridMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Auto", true, state->division == model::GridDivision::automatic);
    for (std::size_t i = 0; i < quantiseDivisions.size(); ++i)
        menu.addItem(static_cast<int>(i) + 2, model::toString(quantiseDivisions[i]), true,
                     state->division == quantiseDivisions[i]);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&gridButton), [this](int choice) {
        if (choice == 1)
            state->division = model::GridDivision::automatic;
        else if (choice >= 2)
            state->division = quantiseDivisions[static_cast<std::size_t>(choice - 2)];
        updateToolbar();
        repaint();
    });
}

void PianoRoll::showQuantiseMenu()
{
    juce::PopupMenu menu;
    for (std::size_t i = 0; i < quantiseDivisions.size(); ++i)
        menu.addItem(static_cast<int>(i) + 1, model::toString(quantiseDivisions[i]), true,
                     state->quantise.division == quantiseDivisions[i]);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&quantiseValue), [this](int choice) {
        if (choice >= 1)
            state->quantise.division = quantiseDivisions[static_cast<std::size_t>(choice - 1)];
        updateToolbar();
    });
}

void PianoRoll::showLanesMenu()
{
    using Type = model::ControllerType;
    juce::PopupMenu menu;
    const auto addLane = [this](juce::PopupMenu& into, const Type& type) {
        into.addItem(controllerName(type), true, controllers->isShown(type), [this, type] {
            if (controllers->isShown(type))
                controllers->hide(type);
            else
                controllers->show(type);
        });
    };
    for (const auto& type : {Type::pitchBend(), Type::controlChange(1), Type::controlChange(64),
                             Type::channelPressure(), Type::controlChange(7), Type::controlChange(11)})
        addLane(menu, type);

    juce::PopupMenu others;
    for (int first = 0; first < 128; first += 16)
    {
        juce::PopupMenu range;
        for (int number = first; number < first + 16; ++number)
            addLane(range, Type::controlChange(number));
        others.addSubMenu("CC" + juce::String(first) + " to CC" + juce::String(first + 15), range);
    }
    menu.addSubMenu("Other Controllers", others);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&lanesButton));
}

void PianoRoll::quantise()
{
    if (state->clipId == 0)
        return;
    state->setNotes(controller::quantised(state->notes(), state->selection, state->frame(),
                                          state->session.getProject().getTempoMap(), state->quantise),
                    "Quantise");
}

void PianoRoll::transpose(int semitones)
{
    if (state->clipId == 0)
        return;
    if (const auto moved = controller::transposed(state->notes(), state->selection, semitones))
        state->setNotes(*moved, "Transpose");
}

void PianoRoll::selectAll()
{
    state->selection.clear();
    for (const auto& note : state->notes())
        state->selection.insert(note.id);
    repaint();
}

void PianoRoll::deleteSelection()
{
    if (state->clipId == 0 || state->selection.empty())
        return;
    state->session.removeNotes(state->clipId, {state->selection.begin(), state->selection.end()});
    state->selection.clear();
    repaint();
}

void PianoRoll::duplicateSelection()
{
    if (state->clipId == 0 || state->selection.empty())
        return;
    const auto notes = state->notes();
    model::Ticks first = std::numeric_limits<model::Ticks>::max();
    model::Ticks last = 0;
    std::int32_t grabbed = 0;
    for (const auto& note : notes)
        if (state->selection.contains(note.id))
        {
            first = std::min(first, note.start);
            last = std::max(last, note.getEnd());
            grabbed = note.id;
        }
    const auto result = controller::movedNotes(notes, state->selection, grabbed, last - first, 0,
                                               state->frame(), {}, true);
    state->selection = result.selection;
    state->setNotes(result.notes, "Duplicate Notes");
}

bool PianoRoll::hasSelection() const noexcept
{
    return !state->selection.empty();
}

void PianoRoll::setCoop(const controller::CoopSession* coop)
{
    state->coop = coop;
}

void PianoRoll::fillPresence(coop::Presence& presence) const
{
    presence.clip = state->clipId;
    presence.cursorKey = state->clipId != 0 ? state->pointerKey : -1;
    presence.clipCursorPosition = state->pointerPosition;
    presence.selectedNotes.assign(state->selection.begin(), state->selection.end());
}

bool PianoRoll::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey))
    {
        const int step = mods.isShiftDown() ? 12 : 1;
        transpose(key.isKeyCode(juce::KeyPress::upKey) ? step : -step);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::backspaceKey) || key.isKeyCode(juce::KeyPress::deleteKey))
    {
        deleteSelection();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::escapeKey))
    {
        state->selection.clear();
        repaint();
        return true;
    }
    if (mods.isCommandDown() && key.getKeyCode() == 'A')
    {
        selectAll();
        return true;
    }
    if (mods.isCommandDown() && key.getKeyCode() == 'D')
    {
        duplicateSelection();
        return true;
    }
    if (!mods.isAnyModifierKeyDown() && key.getKeyCode() == 'Q')
    {
        quantise();
        return true;
    }
    return false;
}

void PianoRoll::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop(toolbarHeight).reduced(6, 3);
    title.setBounds(bar.removeFromLeft(std::min(220, bar.getWidth() / 4)));
    gridButton.setBounds(bar.removeFromLeft(90));
    bar.removeFromLeft(12);
    quantiseValue.setBounds(bar.removeFromLeft(80));
    bar.removeFromLeft(4);
    strength.setBounds(bar.removeFromLeft(150));
    bar.removeFromLeft(4);
    ends.setBounds(bar.removeFromLeft(64));
    bar.removeFromLeft(4);
    quantiseButton.setBounds(bar.removeFromLeft(80));
    lanesButton.setBounds(bar.removeFromRight(80));

    const int lanesHeight = velocityHeight + controllers->preferredHeight();
    auto lanes = area.removeFromBottom(std::min(lanesHeight, area.getHeight() / 2));
    lanes.removeFromLeft(keyboardWidth);
    velocities->setBounds(lanes.removeFromTop(velocityHeight));
    controllers->setBounds(lanes);

    auto top = area.removeFromTop(rulerHeight);
    top.removeFromLeft(keyboardWidth);
    ruler->setBounds(top);
    keyboard->setBounds(area.removeFromLeft(keyboardWidth));
    grid->setBounds(area);

    state->viewport.timeline.setWidth(std::max(1, grid->getWidth()));
    state->viewport.setHeight(std::max(1, grid->getHeight()));
    showPendingView();
}

void PianoRoll::showPendingView()
{
    if (!pendingView || grid->getWidth() < 2 || grid->getHeight() < 2)
        return;
    state->viewport.timeline.showRange(pendingView->start, pendingView->end);
    state->viewport.showKeys(pendingView->lowKey, pendingView->highKey);
    pendingView.reset();
}

void PianoRoll::paint(juce::Graphics& g)
{
    const auto& look = ThemedLookAndFeel::get(*this);
    look.drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::panel, false);
}

} // namespace daw::ui::desktop
