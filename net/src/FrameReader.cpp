#include "daw/net/FrameReader.h"

namespace daw::net
{

juce::MemoryBlock streamBytesOf(const juce::MemoryBlock& frame)
{
    const auto size = static_cast<std::uint32_t>(frame.getSize());
    const std::uint8_t length[] = {static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8),
                                   static_cast<std::uint8_t>(size >> 16),
                                   static_cast<std::uint8_t>(size >> 24)};
    juce::MemoryBlock bytes(length, sizeof(length));
    bytes.append(frame.getData(), frame.getSize());
    return bytes;
}

bool FrameReader::add(const void* data, std::size_t size)
{
    buffer.append(data, size);

    std::size_t position = 0;
    const auto* bytes = static_cast<const std::uint8_t*>(buffer.getData());
    while (buffer.getSize() - position >= 4)
    {
        const auto length = static_cast<std::size_t>(bytes[position])
                            | static_cast<std::size_t>(bytes[position + 1]) << 8
                            | static_cast<std::size_t>(bytes[position + 2]) << 16
                            | static_cast<std::size_t>(bytes[position + 3]) << 24;
        if (length > maximumSize)
            return false;
        if (buffer.getSize() - position - 4 < length)
            break;
        if (length > 0)
            frames.emplace_back(bytes + position + 4, length);
        position += 4 + length;
    }

    if (position > 0)
        buffer.removeSection(0, position);
    return true;
}

std::vector<juce::MemoryBlock> FrameReader::takeFrames()
{
    return std::exchange(frames, {});
}

} // namespace daw::net
