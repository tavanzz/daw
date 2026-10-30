#include "daw/ui/desktop/TrackLanes.h"

#include "daw/controller/ClipEditing.h"

#include <algorithm>
#include <array>

namespace daw::ui::desktop
{

namespace
{

constexpr float edgeWidth = 6.0f;
constexpr int nameHeight = 15;
// How long a clip whose length is not known yet (its file is loading) looks.
constexpr model::Ticks placeholderLength = 2 * model::ticksPerSecond;

const juce::Colour playheadColour(0xffffa726);

juce::Colour trackColour(int trackIndex)
{
    static constexpr std::array<juce::uint32, 8> palette{0xff4fc3f7, 0xff81c784, 0xffffb74d,
                                                         0xffba68c8, 0xffe57373, 0xff4db6ac,
                                                         0xfff06292, 0xffaed581};
    return juce::Colour(palette[static_cast<std::size_t>(trackIndex) % palette.size()]);
}

bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;wave;aif;aiff;aifc");
}

} // namespace

TrackLanes::TrackLanes(ArrangeState& sharedState)
    : state(sharedState)
{
}

void TrackLanes::setPlayhead(model::Ticks position)
{
    if (position == playhead)
        return;

    const auto oldX = static_cast<int>(state.viewport.ticksToX(playhead));
    const auto newX = static_cast<int>(state.viewport.ticksToX(position));
    playhead = position;
    repaint(oldX - 2, 0, 5, getHeight());
    repaint(newX - 2, 0, 5, getHeight());
}

void TrackLanes::paint(juce::Graphics& g)
{
    const auto& project = state.session.getProject();
    const auto& viewport = state.viewport;
    const auto width = static_cast<float>(getWidth());
    const auto height = static_cast<float>(getHeight());

    g.fillAll(juce::Colour(0xff1b1b1b));

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        g.setColour(t % 2 == 0 ? juce::Colour(0xff222222) : juce::Colour(0xff252525));
        g.fillRect(0, t * ArrangeState::laneHeight, getWidth(), ArrangeState::laneHeight);
    }

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
            g.setColour(barLine ? juce::Colour(0xff3c3c3c) : juce::Colour(0xff2c2c2c));
            g.drawVerticalLine(static_cast<int>(viewport.ticksToX(line)), 0.0f, height);
        }
    }

    // Loop region.
    if (project.getLoopEnd() > project.getLoopStart())
    {
        const auto x0 = static_cast<float>(viewport.ticksToX(project.getLoopStart()));
        const auto x1 = static_cast<float>(viewport.ticksToX(project.getLoopEnd()));
        g.setColour(juce::Colour(0xff2e7d32).withAlpha(project.isLoopEnabled() ? 0.12f : 0.05f));
        g.fillRect(juce::Rectangle<float>(x0, 0.0f, x1 - x0, height));
    }

    // Clips, bottom of each stack first so the one on top is drawn last.
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
            drawClip(g, t, track.getClip(c));
    }

    if (dropIndicator)
    {
        g.setColour(juce::Colour(0xff4fc3f7));
        g.drawRect(*dropIndicator, 2.0f);
    }

    const auto x = static_cast<float>(viewport.ticksToX(playhead));
    g.setColour(playheadColour);
    g.drawLine(x, 0.0f, x, height, 1.5f);
    juce::ignoreUnused(width);
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
    if (bounds.getRight() < 0.0f || bounds.getX() > static_cast<float>(getWidth()))
        return;

    const auto* source = state.session.findSource(clip.getAudioFile());
    using Status = engine::io::PooledSource::Status;
    const bool ready = source != nullptr && source->status == Status::ready;
    const bool failed = source != nullptr && source->status == Status::failed;
    const bool selected = state.selection.contains(clip.getId());

    const auto colour = failed ? juce::Colour(0xffc62828) : trackColour(trackIndex);
    g.setColour(colour.withAlpha(ready ? 0.35f : 0.18f));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Keep everything inside the clip: long clips may reach far outside the view.
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(bounds.toNearestInt());

    if (ready && source->peaks != nullptr)
    {
        g.setColour(colour.brighter(0.4f));
        drawWaveform(g, bounds.withTrimmedTop(nameHeight).reduced(0.0f, 2.0f), clip, *source);
    }

    g.setColour(colour.withAlpha(0.6f));
    g.fillRect(bounds.withHeight(static_cast<float>(nameHeight)));

    auto name = clip.getAudioFile().getFileNameWithoutExtension();
    if (failed)
        name = "Missing: " + name;
    else if (!ready)
        name = "Loading " + name + "...";

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(12.0f));
    const auto textArea = bounds.withHeight(static_cast<float>(nameHeight)).reduced(4.0f, 0.0f);
    g.drawText(name, textArea.withX(std::max(textArea.getX(), 2.0f)),
               juce::Justification::centredLeft, true);

    g.setColour(selected ? juce::Colours::white : colour.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, selected ? 2.0f : 1.0f);
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

void TrackLanes::mouseMove(const juce::MouseEvent& event)
{
    const auto hit = clipAt(event.position);
    setMouseCursor(hit && hit->zone != Zone::body ? juce::MouseCursor::LeftRightResizeCursor
                                                   : juce::MouseCursor::NormalCursor);
}

void TrackLanes::mouseDown(const juce::MouseEvent& event)
{
    const auto hit = clipAt(event.position);

    if (!hit)
    {
        if (!event.mods.isShiftDown())
            state.selection.clear();
        state.session.seek(state.snap(state.viewport.xToTicks(event.position.x), event.mods));
        repaint();
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
    repaint();
}

void TrackLanes::mouseDrag(const juce::MouseEvent& event)
{
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

    repaint();
}

void TrackLanes::mouseUp(const juce::MouseEvent&)
{
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

    dropIndicator = juce::Rectangle<float>(left, top, static_cast<float>(getWidth()) - left, height);
    repaint();
}

void TrackLanes::fileDragExit(const juce::StringArray&)
{
    dropIndicator.reset();
    repaint();
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
    repaint();
}

} // namespace daw::ui::desktop
