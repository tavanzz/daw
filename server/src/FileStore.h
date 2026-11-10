#pragma once

#include "daw/net/Tls.h"

#include <juce_core/juce_core.h>

#include <map>
#include <memory>

namespace daw::server
{

/**
    The files of one session, by the SHA-256 of their content: each kept once whatever
    names it, complete ones as <hash>, uploads in progress as <hash>.<size>.part (so they
    resume after a restart). An upload must start with query(), which says how much is
    there; chunks are taken only at that offset; the hash is updated as they come, and a
    file whose content does not have its hash is thrown away.
*/
class FileStore
{
public:
    struct Status
    {
        std::uint64_t stored = 0;
        bool complete = false;
        bool refused = false;
    };

    /** maximumBytes: what this session may keep, uploads in progress included; room():
        what the whole server has left. */
    FileStore(juce::File folder, std::uint64_t maximumBytes, std::uint64_t maximumFileBytes);

    [[nodiscard]] Status query(const juce::MemoryBlock& hash, std::uint64_t size,
                               std::uint64_t serverRoom);
    /** Writes a chunk at offset if the upload is there; completedNow when that was the last
        one and the content has its hash. */
    [[nodiscard]] Status write(const juce::MemoryBlock& hash, std::uint64_t offset,
                               const juce::MemoryBlock& data, bool& completedNow);

    [[nodiscard]] bool has(const juce::MemoryBlock& hash) const;
    [[nodiscard]] std::uint64_t sizeOf(const juce::MemoryBlock& hash) const;
    [[nodiscard]] juce::MemoryBlock read(const juce::MemoryBlock& hash, std::uint64_t offset,
                                         std::uint64_t length) const;

    /** Bytes kept, complete or not. */
    [[nodiscard]] std::uint64_t getBytes() const noexcept { return bytes; }

private:
    struct Upload
    {
        std::uint64_t size = 0;
        std::uint64_t stored = 0;
        std::unique_ptr<net::Sha256> hasher; // over [0, stored)
    };

    [[nodiscard]] juce::File completeFile(const juce::String& hex) const;
    [[nodiscard]] juce::File partFile(const juce::String& hex, std::uint64_t size) const;

    juce::File folder;
    std::uint64_t maximumBytes;
    std::uint64_t maximumFileBytes;
    std::uint64_t bytes = 0;
    std::map<juce::String, std::uint64_t> complete; ///< hex hash to size
    std::map<juce::String, Upload> uploads;
};

} // namespace daw::server
