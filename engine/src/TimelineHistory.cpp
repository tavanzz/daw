#include "daw/engine/TimelineHistory.h"

#include <algorithm>

namespace daw::engine
{

void TimelineHistory::reset(juce::int64 stream, juce::int64 position) noexcept
{
    newest = 0;
    count = 1;
    stretches[0] = {stream, position, Kind::stopped, false};
}

void TimelineHistory::record(const Stretch& stretch) noexcept
{
    const auto& last = stretches[static_cast<std::size_t>(newest)];
    const juce::int64 expected =
        last.kind == Kind::stopped ? last.position : last.position + (stretch.stream - last.stream);

    if (stretch.kind == last.kind && stretch.clicks == last.clicks && stretch.position == expected)
        return;

    // A stretch replacing one that never got a sample.
    if (stretch.stream <= last.stream)
    {
        stretches[static_cast<std::size_t>(newest)] = stretch;
        return;
    }

    newest = (newest + 1) % capacity;
    count = std::min(capacity, count + 1);
    stretches[static_cast<std::size_t>(newest)] = stretch;
}

int TimelineHistory::find(juce::int64 stream) const noexcept
{
    // Newest first: what is asked for is almost always recent.
    int index = newest;
    for (int i = 0; i < count; ++i)
    {
        if (stretches[static_cast<std::size_t>(index)].stream <= stream)
            return index;
        index = (index + capacity - 1) % capacity;
    }
    return -1;
}

TimelineHistory::Stretch TimelineHistory::positioned(int index, juce::int64 stream) const noexcept
{
    if (index < 0)
        return {stream, stretches[static_cast<std::size_t>(oldest())].position, Kind::stopped,
                false};

    auto stretch = stretches[static_cast<std::size_t>(index)];
    if (stretch.kind != Kind::stopped)
        stretch.position += std::max<juce::int64>(0, stream - stretch.stream);
    stretch.stream = std::max(stretch.stream, stream);
    return stretch;
}

TimelineHistory::Stretch TimelineHistory::at(juce::int64 stream) const noexcept
{
    return positioned(find(stream), stream);
}

} // namespace daw::engine
