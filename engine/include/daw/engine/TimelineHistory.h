#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

namespace daw::engine
{

/**
    Which timeline position the engine read at each stream index (samples since the
    engine started), for the last few changes. Effects delay audio, so a point of
    the mix that lags by some latency hears what was read that many samples
    earlier: this says where that was, through loops, seeks, stops and count-ins.

    Audio thread only (or the thread that renders); real-time safe, fixed size.
*/
class TimelineHistory
{
public:
    static constexpr int capacity = 256;

    enum class Kind : std::uint8_t
    {
        stopped, ///< the timeline does not run: one position throughout
        playing, ///< the timeline runs, one sample per stream sample
        countIn  ///< clicks before recording: positions run, the timeline is silent
    };

    struct Stretch
    {
        juce::int64 stream = 0;   ///< the first stream index of the stretch
        juce::int64 position = 0; ///< the timeline position read there
        Kind kind = Kind::stopped;
        bool clicks = false; ///< whether the metronome clicks on it
    };

    /** Forgets everything: the timeline stood at position from stream on. */
    void reset(juce::int64 stream, juce::int64 position) noexcept;

    /** From streamStart on (later than anything recorded before), the engine reads
        what stretch describes. A stretch that just goes on where the last one would
        be by now is merged into it. */
    void record(const Stretch& stretch) noexcept;

    /** What was read at stream index stream, with position moved on to that index.
        Before the oldest stretch kept: stopped where that one starts. */
    [[nodiscard]] Stretch at(juce::int64 stream) const noexcept;

    /** Calls callback(offset, length, stretch) for each piece of [streamStart,
        streamStart + length) that one stretch covers, in order; stretch.position is
        the position at the piece's start. */
    template <typename Callback>
    void forEach(juce::int64 streamStart, int length, Callback&& callback) const noexcept
    {
        int offset = 0;
        while (offset < length)
        {
            const juce::int64 stream = streamStart + offset;
            const int index = find(stream);
            const auto stretch = positioned(index, stream);

            int piece = length - offset;
            const int next = index < 0 ? oldest() : (index != newest ? (index + 1) % capacity : -1);
            if (next >= 0)
                piece = static_cast<int>(std::min<juce::int64>(
                    piece, stretches[static_cast<std::size_t>(next)].stream - stream));

            callback(offset, piece, stretch);
            offset += piece;
        }
    }

private:
    /** -1 for a stream index before the oldest stretch. */
    [[nodiscard]] int find(juce::int64 stream) const noexcept;
    [[nodiscard]] int oldest() const noexcept { return (newest - count + 1 + capacity) % capacity; }
    [[nodiscard]] Stretch positioned(int index, juce::int64 stream) const noexcept;

    std::array<Stretch, capacity> stretches{};
    int newest = 0;
    int count = 1;
};

} // namespace daw::engine
