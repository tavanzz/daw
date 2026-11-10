#pragma once

#include "daw/model/ProjectChanges.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>

namespace daw::coop
{

/**
    Writes the binary form of what co-op peers exchange: little-endian integers, LEB128
    variable-length unsigned integers (signed ones zig-zag encoded), doubles bit for bit,
    UTF-8 strings and byte blocks with their length, juce::vars with their exact type, and
    project trees, paths and changes.

    The format is the network protocol (model::networkProtocolVersion): changing it means
    a new protocol version.
*/
class Writer
{
public:
    void writeByte(std::uint8_t value);
    void writeUInt32(std::uint32_t value);
    void writeVarUInt(std::uint64_t value);
    void writeVarInt(std::int64_t value);
    void writeBool(bool value) { writeByte(value ? 1 : 0); }
    void writeDouble(double value);
    void writeString(const juce::String& text);
    void writeBytes(const void* data, std::size_t size);
    void writeBlock(const juce::MemoryBlock& block);

    /** A juce::var with its exact type (void, bool, int, int64, double, string, binary,
        array). Objects and methods cannot be written. */
    void writeVar(const juce::var& value);
    void writeTree(const juce::ValueTree& tree);
    void writePath(const model::NodePath& path);
    void writeChange(const model::ProjectChange& change);
    void writeChanges(const model::ChangeSet& changes);

    [[nodiscard]] const juce::MemoryBlock& getData() const noexcept { return data; }
    [[nodiscard]] juce::MemoryBlock takeData() { return std::move(data); }

private:
    juce::MemoryBlock data;
};

/**
    Reads what Writer wrote. Reading past the end or anything malformed (an unknown type
    tag, a length beyond the data, trees nested too deep) marks the reader as failed and
    makes every later read return a default value: check isOk() once at the end.
    Untrusted input: nothing it reads is assumed valid.
*/
class Reader
{
public:
    explicit Reader(const juce::MemoryBlock& source) noexcept;
    Reader(const void* data, std::size_t size) noexcept;

    [[nodiscard]] std::uint8_t readByte();
    [[nodiscard]] std::uint32_t readUInt32();
    [[nodiscard]] std::uint64_t readVarUInt();
    [[nodiscard]] std::int64_t readVarInt();
    [[nodiscard]] bool readBool() { return readByte() != 0; }
    [[nodiscard]] double readDouble();
    [[nodiscard]] juce::String readString();
    [[nodiscard]] juce::MemoryBlock readBlock();
    /** A count of things to read next, each taking at least minimumSize bytes: fails when
        the data left cannot hold them (a guard against huge allocations). */
    [[nodiscard]] std::size_t readCount(std::size_t minimumSize = 1);

    [[nodiscard]] juce::var readVar();
    [[nodiscard]] juce::ValueTree readTree();
    [[nodiscard]] model::NodePath readPath();
    [[nodiscard]] model::ProjectChange readChange();
    [[nodiscard]] model::ChangeSet readChanges();

    [[nodiscard]] bool isOk() const noexcept { return ok; }
    [[nodiscard]] bool isAtEnd() const noexcept { return position == size; }
    void fail() noexcept { ok = false; }

private:
    [[nodiscard]] bool canRead(std::size_t bytes) noexcept;
    [[nodiscard]] juce::var readVar(int depth);
    [[nodiscard]] juce::ValueTree readTree(int depth);

    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    std::size_t position = 0;
    bool ok = true;
};

} // namespace daw::coop
