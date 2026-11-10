#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace daw::net
{

/** Frames on a byte stream: a 32-bit little-endian length, then that many bytes. An empty
    frame keeps a quiet connection alive and is not passed on. */
[[nodiscard]] juce::MemoryBlock streamBytesOf(const juce::MemoryBlock& frame);

/** Cuts a byte stream back into frames, however it arrives. */
class FrameReader
{
public:
    explicit FrameReader(std::size_t maximumFrameSize) noexcept
        : maximumSize(maximumFrameSize)
    {
    }

    /** Adds bytes; false when a frame announces more than the maximum (the stream is not
        ours, or hostile: close it). */
    [[nodiscard]] bool add(const void* data, std::size_t size);

    /** Complete frames so far, oldest first. */
    [[nodiscard]] std::vector<juce::MemoryBlock> takeFrames();

private:
    std::size_t maximumSize;
    juce::MemoryBlock buffer;
    std::vector<juce::MemoryBlock> frames;
};

} // namespace daw::net
