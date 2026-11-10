#include "daw/ui/desktop/TrackLanes.h"

#include "daw/controller/ClipEditing.h"
#include "daw/controller/MidiTakes.h"
#include "daw/ui/desktop/CoopPanels.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>

namespace daw::ui::desktop
{

namespace
{

constexpr float edgeWidth = 6.0f;
// How long a clip whose length is not known yet (its file is loading) looks.
constexpr model::Ticks placeholderLength = 2 * model::ticksPerSecond;

using ClipState = ThemedLookAndFeel::ClipState;

bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;wave;aif;aiff;aifc");
}

bool isMidiFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("mid;midi;smf");
}

// Notes drawn small in a clip: the clip's keys spread over its height.
void drawNotes(juce::Graphics& g, juce::Rectangle<float> area, const std::vector<model::Note>& notes,
               model::Ticks origin, const controller::TimelineViewport& viewport)
{
    if (notes.empty())
        return;
    int lowest = 127;
    int highest = 0;
    for (const auto& note : notes)
    {
        lowest = std::min(lowest, note.key);
        highest = std::max(highest, note.key);
    }
    lowest = std::max(0, lowest - 2);
    highest = std::min(127, highest + 2);
    const float rowHeight = area.getHeight() / static_cast<float>(highest - lowest + 1);
    const float noteHeight = std::max(1.5f, rowHeight - 1.0f);

    for (const auto& note : notes)
    {
        const auto x0 = static_cast<float>(viewport.ticksToX(origin + note.start));
        const auto x1 = static_cast<float>(viewport.ticksToX(origin + note.getEnd()));
        if (x1 < area.getX() || x0 > area.getRight())
            continue;
        const float y = area.getBottom() - static_cast<float>(note.key - lowest + 1) * rowHeight;
        g.fillRect(juce::Rectangle<float>(x0, y, std::max(1.0f, x1 - x0 - 1.0f), noteHeight));
    }
}

} // namespace

TrackLanes::TrackLanes(ArrangeState& sharedState)
    : state(sharedState)
{
    // Frames only while the theme draws something that moves.
    updateAnimations();
}

void TrackLanes::contentChanged()
{
    contentValid = false;
    repaint();
}

void TrackLanes::lookAndFeelChanged()
{
    updateAnimations();
    contentChanged();
}

void TrackLanes::resized()
{
    contentChanged();
}

void TrackLanes::updateAnimations()
{
    const auto& look = ThemedLookAndFeel::get(*this);
    backgroundLink.setWantsFrames(look.hasTimelineBackground());
    playheadLink.setWantsFrames(look.playheadAnimates(playing));
}

juce::Rectangle<int> TrackLanes::visibleArea() const
{
    // The lanes scroll inside a viewport: only what it shows is worth painting.
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        return getLocalArea(viewport, viewport->getLocalBounds()).getIntersection(getLocalBounds());

    return getLocalBounds();
}

void TrackLanes::setPlayhead(model::Ticks position, bool isPlaying)
{
    if (isPlaying != playing)
    {
        playing = isPlaying;
        updateAnimations();
        repaintPlayhead();
    }

    if (position == playhead)
        return;

    repaintPlayhead();
    playhead = position;
    repaintPlayhead();
}

void TrackLanes::repaintPlayhead()
{
    const auto x = static_cast<int>(state.viewport.ticksToX(playhead));
    const int reach = ThemedLookAndFeel::get(*this).playheadReach();
    repaint(x - reach, 0, 2 * reach + 1, getHeight());
}

void TrackLanes::paint(juce::Graphics& g)
{
    // The lanes, grid and clips change only with edits and the view: they are painted
    // into an image of the visible area once, and each frame of an animation (the
    // background, the playhead) puts that image back instead of painting them again.
    const auto& look = ThemedLookAndFeel::get(*this);
    const auto visible = visibleArea();
    const bool background = backgroundLink.isActive() && look.hasTimelineBackground();
    const bool under = background && look.timelineBackgroundIsUnder();
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    if (!contentValid || cacheArea != visible || !juce::exactlyEqual(cacheScale, scale) ||
        cacheTranslucent != under)
    {
        cacheArea = visible;
        cacheScale = scale;
        cacheTranslucent = under;
        contentValid = true;

        const auto pixels = visible.toFloat() * scale;
        cache = juce::Image(juce::Image::ARGB, std::max(1, juce::roundToInt(pixels.getWidth())),
                            std::max(1, juce::roundToInt(pixels.getHeight())), true);
        juce::Graphics content(cache);
        content.addTransform(juce::AffineTransform::translation(static_cast<float>(-visible.getX()),
                                                                static_cast<float>(-visible.getY()))
                                 .scaled(scale));
        paintContent(content, under);
    }

    if (under)
    {
        look.drawPanel(g, visible.toFloat(), appearance::ColourRole::window, false);
        look.drawTimelineBackground(g, visible.toFloat(), backgroundLink.getTime());
    }

    // Images are drawn at the context's opacity: the background may have left it low.
    g.setOpacity(1.0f);
    g.drawImageTransformed(cache, juce::AffineTransform::scale(1.0f / scale)
                                      .translated(static_cast<float>(visible.getX()),
                                                  static_cast<float>(visible.getY())));

    if (background && !under)
        look.drawTimelineBackground(g, visible.toFloat(), backgroundLink.getTime());

    look.drawPlayhead(g, static_cast<float>(state.viewport.ticksToX(playhead)), 0.0f,
                             static_cast<float>(getHeight()),
                             {playheadLink.getTime(), playheadLink.isActive(), playing});

    if (state.inCoop())
        paintOthers(g);
}

void TrackLanes::paintOthers(juce::Graphics& g) const
{
    // The others, each in their colour: the clips they selected, their playhead while
    // they play, and their pointer with their initial.
    const auto& project = state.session.getProject();
    const auto& look = ThemedLookAndFeel::get(*this);
    const auto height = static_cast<float>(getHeight());
    for (const auto& peer : state.coop->getOthers())
    {
        if (!peer.connected)
            continue;
        const auto colour = look.participantColour(peer.colour);
        const auto& presence = peer.presence;

        g.setColour(colour);
        for (const auto clipId : presence.selectedClips)
            if (const auto location = project.findClip(clipId))
                g.drawRect(clipBounds(location->trackIndex, project.getTrack(location->trackIndex)
                                                                .getClip(location->clipIndex))
                               .expanded(1.0f),
                           2.0f);

        if (presence.playing)
        {
            const auto x = static_cast<float>(state.viewport.ticksToX(presence.playhead));
            const float dashes[] = {4.0f, 4.0f};
            g.setColour(colour.withAlpha(0.8f));
            g.drawDashedLine({x, 0.0f, x, height}, dashes, 2, 1.5f);
        }

        if (presence.hasCursor)
            if (const int track = project.indexOfTrack(presence.cursorTrack); track >= 0)
            {
                const auto x = static_cast<float>(state.viewport.ticksToX(presence.cursorPosition));
                const auto top = static_cast<float>(track * ArrangeState::laneHeight);
                g.setColour(colour);
                g.fillRect(juce::Rectangle<float>(x - 1.0f, top + 2.0f, 2.0f,
                                                  static_cast<float>(ArrangeState::laneHeight) - 4.0f));
                drawParticipantChip(g, *this, {x + 3.0f, top + 3.0f, 16.0f, 16.0f}, peer.colour, peer.name);
            }
    }
}

void TrackLanes::notePointer(juce::Point<float> position)
{
    const int row = static_cast<int>(position.y) / ArrangeState::laneHeight;
    const auto& project = state.session.getProject();
    if (position.y >= 0.0f && row < project.getNumTracks())
    {
        const auto track = project.getTrack(row).getId();
        state.pointer = std::make_pair(track, state.viewport.xToTicks(position.x));
        state.lookedAt = track;
    }
    else
        state.pointer.reset();
}

void TrackLanes::mouseExit(const juce::MouseEvent&)
{
    state.pointer.reset();
}

void TrackLanes::paintContent(juce::Graphics& g, bool translucent) const
{
    const auto& project = state.session.getProject();
    const auto& viewport = state.viewport;
    const auto& look = ThemedLookAndFeel::get(*this);
    const auto width = static_cast<float>(getWidth());
    const auto height = static_cast<float>(getHeight());

    if (!translucent)
        look.drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::window, false);

    for (int t = 0; t < state.numRows(); ++t)
        look.drawLane(g, rowBounds(t), t, t < project.getNumTracks(), translucent);

    // Grid: the snapping grid at this zoom, bar lines brighter. Too dense a grid
    // (a fine division zoomed far out) falls back to bars, then to nothing.
    const auto tempoMap = project.getTempoMap();
    auto division = viewport.resolveGrid(project.getGridDivision(), tempoMap);
    const auto lineCount = [&](model::GridDivision d) {
        return static_cast<double>(viewport.getEnd() - viewport.getStart()) /
               model::Grid::divisionTicks(tempoMap, d);
    };
    if (lineCount(division) > width / 4.0)
        division = model::GridDivision::bar;

    if (lineCount(division) <= width / 4.0)
    {
        const model::Grid grid(tempoMap, division);
        for (const auto line : grid.linesBetween(viewport.getStart(), viewport.getEnd() + 1))
        {
            const bool barLine = tempoMap.barStart(tempoMap.barAt(line)) == line;
            look.drawGridLine(g, static_cast<float>(viewport.ticksToX(line)), 0.0f, height,
                                     barLine);
        }
    }

    // Loop region.
    if (project.getLoopEnd() > project.getLoopStart())
    {
        const auto x0 = static_cast<float>(viewport.ticksToX(project.getLoopStart()));
        const auto x1 = static_cast<float>(viewport.ticksToX(project.getLoopEnd()));
        look.drawLoopShade(g, {x0, 0.0f, x1 - x0, height}, project.isLoopEnabled());
    }

    // Clips, bottom of each stack first so the one on top is drawn last.
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
            drawClip(g, t, track.getClip(c));
    }

    drawRecordings(g);

    if (state.showAutomation)
        for (int row = 0; row < state.numRows(); ++row)
            automation.paintRow(g, rowBounds(row), state.ownerOfRow(row), look);

    if (dropIndicator)
        look.drawDropIndicator(g, *dropIndicator);
}

juce::Rectangle<float> TrackLanes::clipBounds(int trackIndex, const model::Clip& clip) const
{
    const auto start = clip.getPosition();
    const auto end = clip.getEnd().value_or(start + placeholderLength);
    const auto x0 = static_cast<float>(state.viewport.ticksToX(start));
    const auto x1 = static_cast<float>(state.viewport.ticksToX(end));

    const auto top = static_cast<float>(trackIndex * ArrangeState::laneHeight + 3);
    return {x0, top, std::max(1.0f, x1 - x0), static_cast<float>(ArrangeState::laneHeight - 6)};
}

void TrackLanes::drawClip(juce::Graphics& g, int trackIndex, const model::Clip& clip) const
{
    const auto bounds = clipBounds(trackIndex, clip);
    if (!g.clipRegionIntersects(bounds.getSmallestIntegerContainer()))
        return;

    if (clip.isMidi())
    {
        const auto& look = ThemedLookAndFeel::get(*this);
        const auto style = look.clipStyle(trackIndex, ClipState::ready, trackIndex);
        const float nameHeight = look.clipHeaderHeight();
        {
            juce::Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(bounds.toNearestInt());
            look.drawClipFrame(g, bounds, style);
            g.setColour(style.waveform);
            drawNotes(g, bounds.withTrimmedTop(nameHeight).reduced(0.0f, 2.0f),
                      model::MidiClip(clip.getState()).getNotes(),
                      clip.getPosition() - clip.getOffset(), state.viewport);
            const auto textArea = bounds.withHeight(nameHeight).reduced(4.0f, 0.0f);
            look.drawClipName(g, textArea.withX(std::max(textArea.getX(), 2.0f)), clip.getName(),
                              style);
        }
        look.drawClipOutline(g, bounds, style, state.selection.contains(clip.getId()));
        return;
    }

    const auto* source = state.session.findSource(clip.getAudioFile());
    using Status = engine::io::PooledSource::Status;
    const bool ready = source != nullptr && source->status == Status::ready;
    const bool failed = source != nullptr && source->status == Status::failed;
    const bool selected = state.selection.contains(clip.getId());

    const auto& look = ThemedLookAndFeel::get(*this);
    const auto style = look.clipStyle(
        trackIndex, failed ? ClipState::failed : ready ? ClipState::ready : ClipState::loading,
        trackIndex);
    const float nameHeight = look.clipHeaderHeight();

    {
        // Keep everything inside the clip: long clips may reach far outside the view.
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(bounds.toNearestInt());

        look.drawClipFrame(g, bounds, style);

        if (ready && source->peaks != nullptr)
        {
            g.setColour(style.waveform);
            drawWaveform(g, bounds.withTrimmedTop(nameHeight).reduced(0.0f, 2.0f), clip, *source);
        }

        auto name = clip.getAudioFile().getFileNameWithoutExtension();
        // In co-op a file someone else brought may still be on its way.
        if (const auto coming = state.inCoop() ? state.coop->describeFile(clip.getAudioFile()) : juce::String();
            !ready && coming.isNotEmpty())
            name = coming + ": " + name;
        else if (failed)
            name = "Missing: " + name;
        else if (!ready)
            name = "Loading " + name + "...";

        const auto textArea = bounds.withHeight(nameHeight).reduced(4.0f, 0.0f);
        look.drawClipName(g, textArea.withX(std::max(textArea.getX(), 2.0f)), name, style);
    }

    look.drawClipOutline(g, bounds, style, selected);
}

void TrackLanes::drawRecordings(juce::Graphics& g) const
{
    const auto& project = state.session.getProject();
    const double rate = state.session.getSampleRate();
    const auto& look = ThemedLookAndFeel::get(*this);
    const float nameHeight = look.clipHeaderHeight();

    for (const auto& take : state.session.getRecording().getLiveTakes())
    {
        const int trackIndex = project.indexOfTrack(take.trackId);
        const auto first = std::max(take.keepStart, take.firstPosition);
        const auto end = take.firstPosition + take.length;
        if (trackIndex < 0 || end <= first)
            continue;

        const auto x0 =
            static_cast<float>(state.viewport.ticksToX(model::samplesToTicks(first, rate)));
        const auto x1 =
            static_cast<float>(state.viewport.ticksToX(model::samplesToTicks(end, rate)));
        const juce::Rectangle<float> bounds(
            x0, static_cast<float>(trackIndex * ArrangeState::laneHeight + 3),
            std::max(1.0f, x1 - x0), static_cast<float>(ArrangeState::laneHeight - 6));

        const auto style = look.clipStyle(trackIndex, ClipState::recording, trackIndex);

        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(bounds.toNearestInt());
        look.drawClipFrame(g, bounds, style);

        // The waveform written so far: file sample s is at take.firstPosition + s.
        if (take.peaks != nullptr && take.peaks->getNumSamples() > 0)
        {
            const auto area = bounds.withTrimmedTop(nameHeight).reduced(0.0f, 2.0f);
            const float middle = area.getCentreY();
            const float halfHeight = area.getHeight() / 2.0f;
            g.setColour(style.waveform);
            for (int x = std::max(0, static_cast<int>(area.getX()));
                 x < std::min(getWidth(), static_cast<int>(area.getRight()) + 1); ++x)
            {
                const auto from =
                    model::ticksToSamples(state.viewport.xToTicks(x), rate) - take.firstPosition;
                const auto to =
                    std::max(from + 1, model::ticksToSamples(state.viewport.xToTicks(x + 1), rate) -
                                           take.firstPosition);
                float low = 0.0f;
                float high = 0.0f;
                for (int channel = 0; channel < take.peaks->getNumChannels(); ++channel)
                {
                    const auto range = take.peaks->getRange(channel, from, to);
                    low = std::min(low, range.min);
                    high = std::max(high, range.max);
                }
                if (high > low)
                    g.drawVerticalLine(x, middle - high * halfHeight,
                                       middle - low * halfHeight + 1.0f);
            }
        }

        look.drawClipName(g, bounds.withHeight(nameHeight).reduced(4.0f, 0.0f),
                                 "Recording " + take.file.getFileNameWithoutExtension(), style);
        look.drawClipOutline(g, bounds, style, false);

        // Recording clips pulse (recordings repaint at every refresh: no frames needed).
        if (look.isAnimating(appearance::AnimationEffect::recordPulse))
            look.drawRecordPulse(g, bounds, style.header,
                                        ThemedLookAndFeel::pulseAt(
                                            look.getAnimationClock()->getTime()));
    }

    // MIDI being recorded: from where the clip will start to where it is heard now,
    // with the notes played so far (held ones up to now).
    const auto& recording = state.session.getRecording();
    const auto takes = recording.getLiveMidiTakes();
    if (takes.empty())
        return;
    const auto now = std::max(playhead, model::Ticks{0});
    for (const auto& take : takes)
    {
        const int trackIndex = project.indexOfTrack(take.trackId);
        const auto keepStart = model::samplesToTicks(recording.getLiveKeepStart(), rate);
        if (trackIndex < 0 || now <= keepStart)
            continue;
        const auto x0 = static_cast<float>(state.viewport.ticksToX(keepStart));
        const auto x1 = static_cast<float>(state.viewport.ticksToX(now));
        const juce::Rectangle<float> bounds(
            x0, static_cast<float>(trackIndex * ArrangeState::laneHeight + 3),
            std::max(1.0f, x1 - x0), static_cast<float>(ArrangeState::laneHeight - 6));
        const auto style = look.clipStyle(trackIndex, ClipState::recording, trackIndex);

        const auto content =
            controller::midiTakeContent(take, model::ticksToSamples(now, rate), rate);
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(bounds.toNearestInt());
        look.drawClipFrame(g, bounds, style);
        g.setColour(style.waveform);
        drawNotes(g, bounds.withTrimmedTop(nameHeight).reduced(0.0f, 2.0f), content.notes,
                  model::samplesToTicks(take.firstPosition, rate), state.viewport);
        look.drawClipName(g, bounds.withHeight(nameHeight).reduced(4.0f, 0.0f), "Recording MIDI",
                          style);
        look.drawClipOutline(g, bounds, style, false);
    }
}

void TrackLanes::drawWaveform(juce::Graphics& g, juce::Rectangle<float> area,
                              const model::Clip& clip, const engine::io::PooledSource& source) const
{
    const auto& peaks = *source.peaks;
    const double rate = state.session.getSampleRate();
    const auto position = clip.getPosition();
    const auto offset = clip.getOffset();
    const float middle = area.getCentreY();
    const float halfHeight = area.getHeight() / 2.0f;

    const auto sampleAt = [&](double x) {
        const auto ticks = state.viewport.xToTicks(x);
        return model::ticksToSamples(offset + (ticks - position), rate);
    };

    const int first = std::max(0, static_cast<int>(area.getX()));
    const int last = std::min(getWidth(), static_cast<int>(area.getRight()) + 1);

    for (int x = first; x < last; ++x)
    {
        const auto from = sampleAt(x);
        const auto to = std::max(from + 1, sampleAt(x + 1));

        float low = 0.0f;
        float high = 0.0f;
        for (int channel = 0; channel < peaks.getNumChannels(); ++channel)
        {
            const auto range = peaks.getRange(channel, from, to);
            low = std::min(low, range.min);
            high = std::max(high, range.max);
        }

        if (high > low)
            g.drawVerticalLine(x, middle - high * halfHeight, middle - low * halfHeight + 1.0f);
    }
}

int TrackLanes::trackIndexAt(float y) const
{
    return static_cast<int>(std::floor(y / static_cast<float>(ArrangeState::laneHeight)));
}

std::optional<TrackLanes::Hit> TrackLanes::clipAt(juce::Point<float> point) const
{
    const auto& project = state.session.getProject();
    const int trackIndex = trackIndexAt(point.y);
    if (trackIndex < 0 || trackIndex >= project.getNumTracks())
        return std::nullopt;

    const auto track = project.getTrack(trackIndex);
    for (int c = track.getNumClips() - 1; c >= 0; --c)
    {
        const auto clip = track.getClip(c);
        const auto bounds = clipBounds(trackIndex, clip);
        if (!bounds.contains(point))
            continue;

        Hit hit{clip.getId(), trackIndex, Zone::body};
        const bool trimmable = clip.getLength().has_value() && bounds.getWidth() > 3.0f * edgeWidth;
        if (trimmable && point.x < bounds.getX() + edgeWidth)
            hit.zone = Zone::startEdge;
        else if (trimmable && point.x > bounds.getRight() - edgeWidth)
            hit.zone = Zone::endEdge;
        return hit;
    }

    return std::nullopt;
}

juce::Rectangle<float> TrackLanes::rowBounds(int row) const
{
    return {0.0f, static_cast<float>(row * ArrangeState::laneHeight), static_cast<float>(getWidth()),
            static_cast<float>(ArrangeState::laneHeight)};
}

void TrackLanes::mouseDoubleClick(const juce::MouseEvent& event)
{
    const int row = trackIndexAt(event.position.y);
    if (state.showAutomation)
    {
        if (row >= 0 && row < state.numRows())
            automation.mouseDoubleClick(event, rowBounds(row), state.ownerOfRow(row));
        return;
    }

    // A MIDI clip opens in the piano roll; an empty part of a MIDI track gets a new
    // clip of one bar, from the bar line before the click, and opens it.
    auto& session = state.session;
    const auto& project = session.getProject();
    if (const auto hit = clipAt(event.position))
    {
        const auto location = project.findClip(hit->clipId);
        if (location && project.getTrack(location->trackIndex).getClip(location->clipIndex).isMidi() &&
            state.onOpenClip)
            state.onOpenClip(hit->clipId);
        return;
    }

    if (row < 0 || row >= project.getNumTracks() || !project.getTrack(row).isMidi())
        return;
    const auto tempoMap = project.getTempoMap();
    const auto bar = tempoMap.barAt(state.viewport.xToTicks(event.position.x));
    const auto clipId = session.addMidiClip(project.getTrack(row).getId(), tempoMap.barStart(bar),
                                            tempoMap.barStart(bar + 1) - tempoMap.barStart(bar));
    if (clipId != 0)
    {
        state.selection = {clipId};
        if (state.onOpenClip)
            state.onOpenClip(clipId);
    }
    contentChanged();
}

void TrackLanes::mouseMove(const juce::MouseEvent& event)
{
    notePointer(event.position);
    if (state.showAutomation)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const auto hit = clipAt(event.position);
    setMouseCursor(hit && hit->zone != Zone::body ? juce::MouseCursor::LeftRightResizeCursor
                                                   : juce::MouseCursor::NormalCursor);
}

void TrackLanes::mouseDown(const juce::MouseEvent& event)
{
    notePointer(event.position);
    if (state.showAutomation)
    {
        const int row = trackIndexAt(event.position.y);
        if (row >= 0 && row < state.numRows())
            automation.mouseDown(event, rowBounds(row), state.ownerOfRow(row));
        else
            state.session.seek(state.snap(state.viewport.xToTicks(event.position.x), event.mods));
        contentChanged();
        return;
    }

    const auto hit = clipAt(event.position);

    if (!hit)
    {
        if (!event.mods.isShiftDown())
            state.selection.clear();
        state.session.seek(state.snap(state.viewport.xToTicks(event.position.x), event.mods));
        contentChanged();
        return;
    }

    if (event.mods.isShiftDown())
    {
        if (!state.selection.erase(hit->clipId))
            state.selection.insert(hit->clipId);
    }
    else if (!state.selection.contains(hit->clipId))
    {
        state.selection = {hit->clipId};
    }

    const auto location = state.session.getProject().findClip(hit->clipId);
    const auto clip = state.session.getProject()
                          .getTrack(location->trackIndex)
                          .getClip(location->clipIndex);
    drag = Drag{*hit, clip.getBounds(), state.viewport.xToTicks(event.position.x), false};
    contentChanged();
}

void TrackLanes::mouseDrag(const juce::MouseEvent& event)
{
    notePointer(event.position);
    if (state.showAutomation)
    {
        automation.mouseDrag(event);
        contentChanged();
        return;
    }

    if (!drag)
        return;

    auto& session = state.session;
    if (!drag->started)
    {
        if (event.getDistanceFromDragStart() < 3)
            return;
        drag->started = true;
        const bool copying = drag->hit.zone == Zone::body && event.mods.isAltDown();
        session.beginGesture(copying ? "Copy Clip" : drag->hit.zone == Zone::body ? "Move Clip" : "Trim Clip");

        // Alt+drag: the copy is what moves; the original stays.
        if (copying)
        {
            const auto& project = session.getProject();
            const auto location = project.findClip(drag->hit.clipId);
            if (location)
            {
                const auto copy = session.copyClip(drag->hit.clipId,
                                                   project.getTrack(location->trackIndex).getId(),
                                                   drag->original.position);
                if (copy != 0)
                {
                    drag->hit.clipId = copy;
                    state.selection = {copy};
                }
            }
        }
    }

    const auto& project = session.getProject();
    const auto location = project.findClip(drag->hit.clipId);
    if (!location)
        return;

    const auto delta = state.viewport.xToTicks(event.position.x) - drag->pointerStart;
    const auto snap =
        session.makeSnapper(state.viewport.getTicksPerPixel(), event.mods.isCommandDown());
    const auto& original = drag->original;

    switch (drag->hit.zone)
    {
    case Zone::body:
    {
        const int lastTrack = project.getNumTracks() - 1;
        const int target = std::clamp(trackIndexAt(event.position.y), 0, lastTrack);
        session.moveClip(drag->hit.clipId, project.getTrack(target).getId(),
                         controller::movedPosition(original.position, delta, snap));
        break;
    }
    case Zone::startEdge:
        session.setClipBounds(drag->hit.clipId,
                              controller::trimmedStart(original, original.position + delta, snap));
        break;
    case Zone::endEdge:
    {
        const auto file =
            project.getTrack(location->trackIndex).getClip(location->clipIndex).getAudioFile();
        const auto end = original.position + original.length.value_or(0);
        session.setClipBounds(drag->hit.clipId,
                              controller::trimmedEnd(original, end + delta,
                                                     session.getFileLength(file), snap));
        break;
    }
    }

    contentChanged();
}

void TrackLanes::mouseUp(const juce::MouseEvent&)
{
    automation.mouseUp();
    contentChanged();
    if (drag && drag->started)
        state.session.endGesture();
    drag.reset();
}

void TrackLanes::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!state.handleWheel(event, wheel))
        Component::mouseWheelMove(event, wheel); // up and down: scroll the tracks
}

void TrackLanes::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    state.handleMagnify(event, scaleFactor);
}

bool TrackLanes::isInterestedInFileDrag(const juce::StringArray& files)
{
    return !files.isEmpty() && std::all_of(files.begin(), files.end(), [](const juce::String& path) {
        return isAudioFile(path) || isMidiFile(path);
    });
}

void TrackLanes::fileDragMove(const juce::StringArray& files, int x, int y)
{
    const auto& project = state.session.getProject();
    const int track = trackIndexAt(static_cast<float>(y));
    const bool ontoTrack = files.size() == 1 && track >= 0 && track < project.getNumTracks();
    const int lane = ontoTrack ? track : project.getNumTracks();
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    const auto position = state.snap(state.viewport.xToTicks(x), mods);
    const auto left = static_cast<float>(state.viewport.ticksToX(position));
    const int lanes = ontoTrack ? 1 : files.size();
    const auto top = static_cast<float>(lane * ArrangeState::laneHeight + 2);
    const auto height = static_cast<float>(lanes * ArrangeState::laneHeight - 4);

    const auto width = static_cast<float>(getWidth()) - left;
    dropIndicator = juce::Rectangle<float>(left, top, width, height);
    contentChanged();
}

void TrackLanes::fileDragExit(const juce::StringArray&)
{
    dropIndicator.reset();
    contentChanged();
}

void TrackLanes::filesDropped(const juce::StringArray& files, int x, int y)
{
    dropIndicator.reset();

    auto& session = state.session;
    const auto& project = session.getProject();
    const int track = trackIndexAt(static_cast<float>(y));
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    const auto position = state.snap(state.viewport.xToTicks(x), mods);

    std::optional<model::ObjectId> ontoTrack;
    if (files.size() == 1 && track >= 0 && track < project.getNumTracks())
        ontoTrack = project.getTrack(track).getId();

    juce::Array<juce::File> audioFiles;
    for (const auto& path : files)
    {
        if (isMidiFile(path))
        {
            const auto import = session.importMidi(juce::File(path), position);
            if (import.result.failed() && onProblem)
                onProblem("The MIDI file could not be imported", import.result.getErrorMessage());
            else if (!import.leftOut.isEmpty() && onProblem)
                onProblem("Imported, with some things left out",
                          juce::File(path).getFileName() + " had " +
                              import.leftOut.joinIntoString(", ") +
                              ", which this version does not keep.");
            continue;
        }
        audioFiles.add(juce::File(path));
    }

    if (!audioFiles.isEmpty())
    {
        const auto clips = session.importAudio(audioFiles, position, ontoTrack);
        state.selection = {clips.begin(), clips.end()};
    }
    contentChanged();
}

} // namespace daw::ui::desktop
