#include "daw/engine/Arrangement.h"

#include <algorithm>

namespace daw::engine
{

namespace
{

struct Interval
{
    juce::int64 start = 0;
    juce::int64 end = 0;
};

// Parts of [start, end) not covered by any interval of covered (sorted, disjoint).
std::vector<Interval> uncovered(juce::int64 start, juce::int64 end,
                                const std::vector<Interval>& covered)
{
    std::vector<Interval> parts;
    juce::int64 position = start;

    for (const auto& interval : covered)
    {
        if (interval.end <= position)
            continue;
        if (interval.start >= end)
            break;
        if (interval.start > position)
            parts.push_back({position, interval.start});
        position = std::max(position, interval.end);
        if (position >= end)
            break;
    }

    if (position < end)
        parts.push_back({position, end});

    return parts;
}

// Adds [start, end) to covered, keeping it sorted and disjoint.
void cover(std::vector<Interval>& covered, juce::int64 start, juce::int64 end)
{
    std::vector<Interval> merged;
    merged.reserve(covered.size() + 1);
    bool placed = false;

    for (const auto& interval : covered)
    {
        if (interval.end < start || placed)
        {
            merged.push_back(interval);
            continue;
        }

        if (interval.start > end)
        {
            merged.push_back({start, end});
            merged.push_back(interval);
            placed = true;
            continue;
        }

        start = std::min(start, interval.start);
        end = std::max(end, interval.end);
    }

    if (!placed)
        merged.push_back({start, end});

    covered = std::move(merged);
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept
{
    // FNV-1a over the 8 bytes of value.
    for (int byte = 0; byte < 8; ++byte)
    {
        hash ^= (value >> (8 * byte)) & 0xff;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

void Arrangement::updateLayoutHash() noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;

    for (const auto& track : tracks)
    {
        hash = mix(hash, static_cast<std::uint64_t>(track.id));
        for (const auto& segment : track.segments)
        {
            const auto source = reinterpret_cast<std::uintptr_t>(segment.source);
            hash = mix(hash, static_cast<std::uint64_t>(source));
            hash = mix(hash, static_cast<std::uint64_t>(segment.start));
            hash = mix(hash, static_cast<std::uint64_t>(segment.end));
            hash = mix(hash, static_cast<std::uint64_t>(segment.sourceStart));
            hash = mix(hash, static_cast<std::uint64_t>(segment.fadeInLength));
            hash = mix(hash, static_cast<std::uint64_t>(segment.fadeOutLength));
        }
    }

    layoutHash = hash;
}

std::vector<ClipSegment> flattenClips(std::span<const ClipPlacement> clips, int fadeLength)
{
    std::vector<ClipSegment> segments;
    std::vector<Interval> covered;

    // From the top down: each clip is heard where nothing above it is.
    for (auto clip = clips.rbegin(); clip != clips.rend(); ++clip)
    {
        if (clip->end <= clip->start)
            continue;

        for (const auto& part : uncovered(clip->start, clip->end, covered))
        {
            if (clip->source == nullptr)
                continue;

            const juce::int64 sourceStart = clip->sourceStart + (part.start - clip->start);
            const juce::int64 sourceEnd = sourceStart + (part.end - part.start);

            // Natural edges: the first and last sample of the file.
            const bool cutStart = sourceStart > 0;
            const bool cutEnd = sourceEnd < clip->source->getLength();

            const juce::int64 length = part.end - part.start;
            const juce::int64 room = cutStart && cutEnd ? length / 2 : length;
            const auto fade = static_cast<int>(std::min<juce::int64>(fadeLength, room));

            segments.push_back({clip->source.get(), part.start, part.end, sourceStart,
                                cutStart ? fade : 0, cutEnd ? fade : 0});
        }

        cover(covered, clip->start, clip->end);
    }

    std::sort(segments.begin(), segments.end(),
              [](const ClipSegment& a, const ClipSegment& b) { return a.start < b.start; });
    return segments;
}

} // namespace daw::engine
