#include "daw/engine/io/WavRepair.h"

#include <optional>

namespace daw::engine::io
{

namespace
{

constexpr juce::uint32 tag(const char (&name)[5]) noexcept
{
    return static_cast<juce::uint32>(static_cast<unsigned char>(name[0])) |
           (static_cast<juce::uint32>(static_cast<unsigned char>(name[1])) << 8) |
           (static_cast<juce::uint32>(static_cast<unsigned char>(name[2])) << 16) |
           (static_cast<juce::uint32>(static_cast<unsigned char>(name[3])) << 24);
}

struct Layout
{
    bool rf64 = false;
    juce::int64 reservedChunk = -1; ///< a "JUNK" or "ds64" chunk's header position, if any
    juce::uint32 reservedSize = 0;
    juce::int64 dataChunk = -1;     ///< the "data" chunk's header position
    int blockAlign = 0;
    int numChannels = 0;
    double sampleRate = 0.0;
};

WavRepairResult failure(const juce::File& file, const juce::String& reason)
{
    WavRepairResult result;
    result.result = juce::Result::fail("\"" + file.getFileName() + "\" " + reason);
    return result;
}

std::optional<Layout> readLayout(juce::FileInputStream& stream, juce::int64 fileSize)
{
    Layout layout;
    const auto riff = static_cast<juce::uint32>(stream.readInt());
    stream.readInt(); // the RIFF size: not to be trusted
    if ((riff != tag("RIFF") && riff != tag("RF64")) ||
        static_cast<juce::uint32>(stream.readInt()) != tag("WAVE"))
        return std::nullopt;
    layout.rf64 = riff == tag("RF64");

    // Chunks up to "data", which runs to the end of what was written. Their sizes
    // are reliable: only the data chunk's grows while recording.
    while (stream.getPosition() + 8 <= fileSize)
    {
        const auto position = stream.getPosition();
        const auto id = static_cast<juce::uint32>(stream.readInt());
        const auto size = static_cast<juce::uint32>(stream.readInt());

        if (id == tag("data"))
        {
            layout.dataChunk = position;
            break;
        }

        if (id == tag("fmt ") && size >= 16)
        {
            stream.readShort(); // format tag
            layout.numChannels = stream.readShort();
            layout.sampleRate = static_cast<double>(static_cast<juce::uint32>(stream.readInt()));
            stream.readInt(); // bytes per second
            layout.blockAlign = stream.readShort();
        }
        else if ((id == tag("JUNK") || id == tag("ds64")) && size >= 28 && layout.reservedChunk < 0)
        {
            layout.reservedChunk = position;
            layout.reservedSize = size;
        }

        if (!stream.setPosition(position + 8 + size + (size & 1)))
            return std::nullopt;
    }

    if (layout.dataChunk < 0 || layout.blockAlign <= 0 || layout.numChannels <= 0)
        return std::nullopt;
    return layout;
}

} // namespace

WavRepairResult repairWav(const juce::File& file, juce::int64 rf64Threshold)
{
    std::optional<Layout> layout;
    juce::int64 fileSize = 0;
    juce::int64 storedDataSize = -1;
    {
        juce::FileInputStream stream(file);
        if (!stream.openedOk())
            return failure(file, "could not be opened.");

        fileSize = stream.getTotalLength();
        layout = readLayout(stream, fileSize);
        if (!layout)
            return failure(file, "is not a WAV recording that can be repaired.");

        // What the header says now, to tell whether anything has to change.
        if (layout->rf64 && layout->reservedChunk >= 0 &&
            stream.setPosition(layout->reservedChunk + 8 + 8))
            storedDataSize = stream.readInt64();
        else if (stream.setPosition(layout->dataChunk + 4))
            storedDataSize = static_cast<juce::uint32>(stream.readInt());
    }

    const juce::int64 dataStart = layout->dataChunk + 8;
    const juce::int64 numFrames =
        std::max(juce::int64{0}, fileSize - dataStart) / layout->blockAlign;
    const juce::int64 dataSize = numFrames * layout->blockAlign;
    const bool needsRf64 = dataSize >= rf64Threshold;
    const juce::int64 end = dataStart + dataSize + (dataSize & 1); // a pad byte after odd data

    WavRepairResult result;
    result.numFrames = numFrames;
    result.sampleRate = layout->sampleRate;
    result.numChannels = layout->numChannels;

    if (storedDataSize == dataSize && fileSize == end && layout->rf64 == needsRf64)
        return result;

    if (needsRf64 && layout->reservedChunk < 0)
        return failure(file, "is too long for its header to be repaired.");

    juce::FileOutputStream stream(file);
    if (!stream.openedOk())
        return failure(file, "could not be written.");

    // Cut a partial last frame, or add the pad byte.
    if (fileSize > end && (!stream.setPosition(end) || stream.truncate().failed()))
        return failure(file, "could not be shortened.");
    if (fileSize < end && (!stream.setPosition(end - 1) || !stream.writeByte(0)))
        return failure(file, "could not be completed.");

    const juce::int64 riffSize = end - 8;
    bool written = stream.setPosition(0);

    if (needsRf64)
    {
        // The reserved chunk becomes "ds64": the 64-bit sizes.
        written = written && stream.writeInt(static_cast<int>(tag("RF64"))) &&
                  stream.writeInt(-1) &&
                  stream.setPosition(layout->reservedChunk) &&
                  stream.writeInt(static_cast<int>(tag("ds64"))) &&
                  stream.writeInt(static_cast<int>(layout->reservedSize)) &&
                  stream.writeInt64(riffSize) && stream.writeInt64(dataSize) &&
                  stream.writeInt64(numFrames) && stream.writeInt(0) &&
                  stream.setPosition(layout->dataChunk + 4) && stream.writeInt(-1);
    }
    else
    {
        if (layout->rf64 && layout->reservedChunk >= 0)
            written = written && stream.setPosition(layout->reservedChunk) &&
                      stream.writeInt(static_cast<int>(tag("JUNK"))) && stream.setPosition(0);

        written = written && stream.writeInt(static_cast<int>(tag("RIFF"))) &&
                  stream.writeInt(static_cast<int>(static_cast<juce::uint32>(riffSize))) &&
                  stream.setPosition(layout->dataChunk + 4) &&
                  stream.writeInt(static_cast<int>(static_cast<juce::uint32>(dataSize)));
    }

    stream.flush();
    if (!written || stream.getStatus().failed())
        return failure(file, "could not be repaired.");

    result.changed = true;
    return result;
}

} // namespace daw::engine::io
