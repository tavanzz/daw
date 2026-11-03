#include "daw/ui/desktop/TrackLanes.h"

#include "daw/controller/ClipEditing.h"
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
    const auto& lookAndFeel = ThemedLookAndFeel::get(*this);
    backgroundLink.setWantsFrames(lookAndFeel.hasTimelineBackground());
    playheadLink.setWantsFrames(lookAndFeel.playheadAnimates(playing));
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
    const auto& lookAndFeel = ThemedLookAndFeel::get(*this);
    const auto visible = visibleArea();
    const bool background = backgroundLink.isActive() && lookAndFeel.hasTimelineBackground();
    const bool under = background && lookAndFeel.timelineBackgroundIsUnder();
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
        lookAndFeel.drawPanel(g, visible.toFloat(), appearance::ColourRole::window, false);
        lookAndFeel.drawTimelineBackground(g, visible.toFloat(), backgroundLink.getTime());
    }

    // Images are drawn at the context's opacity: the background may have left it low.
    g.setOpacity(1.0f);
    g.drawImageTransformed(cache, juce::AffineTransform::scale(1.0f / scale)
                                      .translated(static_cast<float>(visible.getX()),
                                                  static_cast<float>(visible.getY())));

    if (background && !under)
        lookAndFeel.drawTimelineBackground(g, visible.toFloat(), backgroundLink.getTime());

    lookAndFeel.drawPlayhead(g, static_cast<float>(state.viewport.ticksToX(playhead)), 0.0f,
                             static_cast<float>(getHeight()),
                             {playheadLink.getTime(), playheadLink.isActive(), playing});
}

void TrackLanes::paintContent(juce::Graphics& g, bool translucent) const
{
    const auto& project = state.session.getProject();
    const auto& viewport = state.viewport;
    const auto& lookAndFeel = ThemedLookAndFeel::get(*this);
    const auto width = static_cast<float>(getWidth());
    const auto height = static_cast<float>(getHeight());

    if (!translucent)
        lookAndFeel.drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::window, false);

    for (int t = 0; t < state.numRows(); ++t)
        lookAndFeel.drawLane(g, rowBounds(t), t, t < project.getNumTracks(), translucent);

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
            lookAndFeel.drawGridLine(g, static_cast<float>(viewport.ticksToX(line)), 0.0f, height,
                                     barLine);
        }
    }

    // Loop region.
    if (project.getLoopEnd() > project.getLoopStart())
    {
        const auto x0 = static_cast<float>(viewport.ticksToX(project.getLoopStart()));
        const auto x1 = static_cast<float>(viewport.ticksToX(project.getLoopEnd()));
        lookAndFeel.drawLoopShade(g, {x0, 0.0f, x1 - x0, height}, project.isLoopEnabled());
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
            automation.paintRow(g, rowBounds(row), state.ownerOfRow(row), lookAndFeel);

    if (dropIndicator)
        lookAndFeel.drawDropIndicator(g, *dropIndicator);
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

    const auto* source = state.session.findSource(clip.getAudioFile());
    using Status = engine::io::PooledSource::Status;
    const bool ready = source != nullptr && source->status == Status::ready;
    const bool failed = source != nullptr && source->status == Status::failed;
    const bool selected = state.selection.contains(clip.getId());

    const auto& lookAndFeel = ThemedLookAndFeel::get(*this);
    const auto style = lookAndFeel.clipStyle(
        trackIndex, failed ? ClipState::failed : ready ? ClipState::ready : ClipState::loading,
        trackIndex);
    const float nameHeight = lookAndFeel.clipHeaderHeight();

    {
        // Keep everything inside the clip: long clips may reach far outside the view.
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(bounds.toNearestInt());

        lookAndFeel.drawClipFrame(g, bounds, style);

        if (ready && source->peaks != nullptr)
        {
            g.setColour(style.waveform);
            drawWaveform(g, bounds.withTrimmedTop(nameHeight).reduced(0.0f, 2.0f), clip, *source);
        }

        auto name = clip.getAudioFile().getFileNameWithoutExtension();
        if (failed)
            name = "Missing: " + name;
        else if (!ready)
            name = "Loading " + name + "...";

        const auto textArea = bounds.withHeight(nameHeight).reduced(4.0f, 0.0f);
        lookAndFeel.drawClipName(g, textArea.withX(std::max(textArea.getX(), 2.0f)), name, style);
    }

    lookAndFeel.drawClipOutline(g, bounds, style, selected);
}

void TrackLanes::drawRecordings(juce::Graphics& g) const
{
    const auto& project = state.session.getProject();
    const double rate = state.session.getSampleRate();
    const auto& lookAndFeel = ThemedLookAndFeel::get(*this);
    const float nameHeight = lookAndFeel.clipHeaderHeight();

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

        const auto style = lookAndFeel.clipStyle(trackIndex, ClipState::recording, trackIndex);

        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(bounds.toNearestInt());
        lookAndFeel.drawClipFrame(g, bounds, style);

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

        lookAndFeel.drawClipName(g, bounds.withHeight(nameHeight).reduced(4.0f, 0.0f),
                                 "Recording " + take.file.getFileNameWithoutExtension(), style);
        lookAndFeel.drawClipOutline(g, bounds, style, false);

        // Recording clips pulse (recordings repaint at every refresh: no frames needed).
        if (lookAndFeel.isAnimating(appearance::AnimationEffect::recordPulse))
            lookAndFeel.drawRecordPulse(g, bounds, style.header,
                                        ThemedLookAndFeel::pulseAt(
                                            lookAndFeel.getAnimationClock()->getTime()));
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
    if (state.showAutomation && row >= 0 && row < state.numRows())
        automation.mouseDoubleClick(event, rowBounds(row), state.ownerOfRow(row));
}

void TrackLanes::mouseMove(const juce::MouseEvent& event)
{
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
        session.beginGesture(drag->hit.zone == Zone::body ? "Move Clip" : "Trim Clip");
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
    return !files.isEmpty() && std::all_of(files.begin(), files.end(), isAudioFile);
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
        audioFiles.add(juce::File(path));

    const auto clips = session.importAudio(audioFiles, position, ontoTrack);
    state.selection = {clips.begin(), clips.end()};
    contentChanged();
}

} // namespace daw::ui::desktop
