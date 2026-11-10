#include "daw/coop/Wire.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace daw::coop
{

namespace
{

// Type tags of juce::vars.
enum class VarTag : std::uint8_t
{
    voidValue = 0,
    undefined = 1,
    falseValue = 2,
    trueValue = 3,
    intValue = 4,
    int64Value = 5,
    doubleValue = 6,
    stringValue = 7,
    binaryValue = 8,
    arrayValue = 9
};

// How a change's value after is written: binary data and strings that changed in one
// place (notes, plugin states) go as that range only.
enum class AfterTag : std::uint8_t
{
    absent = 0,
    value = 1,
    binaryDelta = 2,
    stringDelta = 3
};

constexpr int maximumDepth = 64;

std::uint64_t zigZag(std::int64_t value) noexcept
{
    return (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
}

std::int64_t unZigZag(std::uint64_t value) noexcept
{
    return static_cast<std::int64_t>(value >> 1) ^ -static_cast<std::int64_t>(value & 1);
}

struct Range
{
    std::size_t prefix = 0;
    std::size_t suffix = 0;
};

// The bytes before and after the one place where two blocks differ.
Range sameEnds(const std::uint8_t* before, std::size_t beforeSize, const std::uint8_t* after,
               std::size_t afterSize)
{
    Range range;
    const auto shorter = std::min(beforeSize, afterSize);
    while (range.prefix < shorter && before[range.prefix] == after[range.prefix])
        ++range.prefix;
    while (range.suffix < shorter - range.prefix
           && before[beforeSize - 1 - range.suffix] == after[afterSize - 1 - range.suffix])
        ++range.suffix;
    return range;
}

juce::MemoryBlock utf8Of(const juce::String& text)
{
    const auto utf8 = text.toUTF8();
    return {utf8.getAddress(), utf8.sizeInBytes() - 1};
}

void writeDelta(Writer& writer, const juce::MemoryBlock& before, const juce::MemoryBlock& after)
{
    const auto* from = static_cast<const std::uint8_t*>(before.getData());
    const auto* to = static_cast<const std::uint8_t*>(after.getData());
    const auto range = sameEnds(from, before.getSize(), to, after.getSize());
    writer.writeVarUInt(range.prefix);
    writer.writeVarUInt(range.suffix);
    writer.writeVarUInt(after.getSize() - range.prefix - range.suffix);
    writer.writeBytes(to + range.prefix, after.getSize() - range.prefix - range.suffix);
}

bool deltaIsWorthIt(const juce::MemoryBlock& before, const juce::MemoryBlock& after)
{
    return before.getSize() > 32 && after.getSize() > 32;
}

std::optional<juce::MemoryBlock> readDelta(Reader& reader, const juce::MemoryBlock& before)
{
    const auto prefix = reader.readVarUInt();
    const auto suffix = reader.readVarUInt();
    const auto middle = reader.readBlock();
    if (!reader.isOk() || prefix > before.getSize() || suffix > before.getSize() - prefix)
        return std::nullopt;

    juce::MemoryBlock after;
    after.append(before.getData(), static_cast<std::size_t>(prefix));
    after.append(middle.getData(), middle.getSize());
    after.append(static_cast<const char*>(before.getData()) + before.getSize() - suffix,
                 static_cast<std::size_t>(suffix));
    return after;
}

} // namespace

// Writer ------------------------------------------------------------------------------------

void Writer::writeByte(std::uint8_t value)
{
    data.append(&value, 1);
}

void Writer::writeUInt32(std::uint32_t value)
{
    const std::uint8_t bytes[] = {static_cast<std::uint8_t>(value),
                                  static_cast<std::uint8_t>(value >> 8),
                                  static_cast<std::uint8_t>(value >> 16),
                                  static_cast<std::uint8_t>(value >> 24)};
    data.append(bytes, sizeof(bytes));
}

void Writer::writeVarUInt(std::uint64_t value)
{
    do
    {
        auto byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if (value != 0)
            byte |= 0x80;
        writeByte(byte);
    } while (value != 0);
}

void Writer::writeVarInt(std::int64_t value)
{
    writeVarUInt(zigZag(value));
}

void Writer::writeDouble(double value)
{
    auto bits = std::bit_cast<std::uint64_t>(value);
    std::uint8_t bytes[8];
    for (auto& byte : bytes)
    {
        byte = static_cast<std::uint8_t>(bits);
        bits >>= 8;
    }
    data.append(bytes, sizeof(bytes));
}

void Writer::writeString(const juce::String& text)
{
    writeBlock(utf8Of(text));
}

void Writer::writeBytes(const void* bytes, std::size_t size)
{
    if (size > 0)
        data.append(bytes, size);
}

void Writer::writeBlock(const juce::MemoryBlock& block)
{
    writeVarUInt(block.getSize());
    writeBytes(block.getData(), block.getSize());
}

void Writer::writeVar(const juce::var& value)
{
    if (value.isVoid())
        writeByte(static_cast<std::uint8_t>(VarTag::voidValue));
    else if (value.isUndefined())
        writeByte(static_cast<std::uint8_t>(VarTag::undefined));
    else if (value.isBool())
        writeByte(static_cast<std::uint8_t>(static_cast<bool>(value) ? VarTag::trueValue
                                                                     : VarTag::falseValue));
    else if (value.isInt())
    {
        writeByte(static_cast<std::uint8_t>(VarTag::intValue));
        writeVarInt(static_cast<int>(value));
    }
    else if (value.isInt64())
    {
        writeByte(static_cast<std::uint8_t>(VarTag::int64Value));
        writeVarInt(static_cast<juce::int64>(value));
    }
    else if (value.isDouble())
    {
        writeByte(static_cast<std::uint8_t>(VarTag::doubleValue));
        writeDouble(static_cast<double>(value));
    }
    else if (value.isString())
    {
        writeByte(static_cast<std::uint8_t>(VarTag::stringValue));
        writeString(value.toString());
    }
    else if (value.isBinaryData())
    {
        writeByte(static_cast<std::uint8_t>(VarTag::binaryValue));
        writeBlock(*value.getBinaryData());
    }
    else if (value.isArray())
    {
        writeByte(static_cast<std::uint8_t>(VarTag::arrayValue));
        writeVarUInt(static_cast<std::uint64_t>(value.size()));
        for (const auto& element : *value.getArray())
            writeVar(element);
    }
    else
    {
        // Objects and methods are never part of a project's state.
        jassertfalse;
        writeByte(static_cast<std::uint8_t>(VarTag::voidValue));
    }
}

void Writer::writeTree(const juce::ValueTree& tree)
{
    writeString(tree.getType().toString());
    writeVarUInt(static_cast<std::uint64_t>(tree.getNumProperties()));
    for (int index = 0; index < tree.getNumProperties(); ++index)
    {
        const auto name = tree.getPropertyName(index);
        writeString(name.toString());
        writeVar(tree[name]);
    }
    writeVarUInt(static_cast<std::uint64_t>(tree.getNumChildren()));
    for (const auto& child : tree)
        writeTree(child);
}

void Writer::writePath(const model::NodePath& path)
{
    writeVarUInt(path.size());
    for (const auto& step : path)
    {
        writeString(step.type.toString());
        writeByte(static_cast<std::uint8_t>(step.key));
        switch (step.key)
        {
        case model::PathStep::Key::id:
            writeVarInt(step.id);
            break;
        case model::PathStep::Key::target:
            writeString(step.target);
            break;
        case model::PathStep::Key::index:
            writeVarUInt(static_cast<std::uint64_t>(step.index));
            break;
        }
    }
}

void Writer::writeChange(const model::ProjectChange& change)
{
    writeByte(static_cast<std::uint8_t>(change.kind));
    writePath(change.path);

    switch (change.kind)
    {
    case model::ProjectChange::Kind::setProperty:
    {
        writeString(change.property.toString());
        writeBool(change.before.has_value());
        if (change.before)
            writeVar(*change.before);

        const auto& before = change.before;
        const auto& after = change.after;
        if (!after)
        {
            writeByte(static_cast<std::uint8_t>(AfterTag::absent));
        }
        else if (before && before->isBinaryData() && after->isBinaryData()
                 && deltaIsWorthIt(*before->getBinaryData(), *after->getBinaryData()))
        {
            writeByte(static_cast<std::uint8_t>(AfterTag::binaryDelta));
            writeDelta(*this, *before->getBinaryData(), *after->getBinaryData());
        }
        else if (before && before->isString() && after->isString()
                 && deltaIsWorthIt(utf8Of(before->toString()), utf8Of(after->toString())))
        {
            writeByte(static_cast<std::uint8_t>(AfterTag::stringDelta));
            writeDelta(*this, utf8Of(before->toString()), utf8Of(after->toString()));
        }
        else
        {
            writeByte(static_cast<std::uint8_t>(AfterTag::value));
            writeVar(*after);
        }
        break;
    }
    case model::ProjectChange::Kind::insertChild:
    case model::ProjectChange::Kind::removeChild:
        writeVarUInt(static_cast<std::uint64_t>(change.index));
        writeTree(change.child);
        break;
    case model::ProjectChange::Kind::moveChild:
        writeVarUInt(static_cast<std::uint64_t>(change.index));
        writeVarUInt(static_cast<std::uint64_t>(change.newIndex));
        break;
    }
}

void Writer::writeChanges(const model::ChangeSet& changes)
{
    writeVarUInt(changes.size());
    for (const auto& change : changes)
        writeChange(change);
}

// Reader ------------------------------------------------------------------------------------

Reader::Reader(const juce::MemoryBlock& source) noexcept
    : Reader(source.getData(), source.getSize())
{
}

Reader::Reader(const void* data, std::size_t dataSize) noexcept
    : bytes(static_cast<const std::uint8_t*>(data)), size(dataSize)
{
}

bool Reader::canRead(std::size_t count) noexcept
{
    if (ok && count <= size - position)
        return true;
    ok = false;
    return false;
}

std::uint8_t Reader::readByte()
{
    return canRead(1) ? bytes[position++] : std::uint8_t{0};
}

std::uint32_t Reader::readUInt32()
{
    if (!canRead(4))
        return 0;
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(bytes[position++]) << shift;
    return value;
}

std::uint64_t Reader::readVarUInt()
{
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 7)
    {
        const auto byte = readByte();
        if (!ok)
            return 0;
        value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0)
            return value;
    }
    ok = false;
    return 0;
}

std::int64_t Reader::readVarInt()
{
    return unZigZag(readVarUInt());
}

double Reader::readDouble()
{
    if (!canRead(8))
        return 0.0;
    std::uint64_t bits = 0;
    for (int shift = 0; shift < 64; shift += 8)
        bits |= static_cast<std::uint64_t>(bytes[position++]) << shift;
    return std::bit_cast<double>(bits);
}

juce::MemoryBlock Reader::readBlock()
{
    const auto length = readVarUInt();
    if (!canRead(static_cast<std::size_t>(length)))
        return {};
    juce::MemoryBlock block(bytes + position, static_cast<std::size_t>(length));
    position += static_cast<std::size_t>(length);
    return block;
}

juce::String Reader::readString()
{
    const auto block = readBlock();
    const auto* text = static_cast<const char*>(block.getData());
    const auto length = static_cast<int>(block.getSize());
    if (length == 0)
        return {};
    if (!juce::CharPointer_UTF8::isValidString(text, length))
    {
        ok = false;
        return {};
    }
    return juce::String::fromUTF8(text, length);
}

std::size_t Reader::readCount(std::size_t minimumSize)
{
    const auto count = readVarUInt();
    if (!ok || count > (size - position) / std::max<std::size_t>(1, minimumSize))
    {
        ok = false;
        return 0;
    }
    return static_cast<std::size_t>(count);
}

juce::var Reader::readVar()
{
    return readVar(0);
}

juce::var Reader::readVar(int depth)
{
    if (depth > maximumDepth)
    {
        ok = false;
        return {};
    }

    switch (static_cast<VarTag>(readByte()))
    {
    case VarTag::voidValue:
        return {};
    case VarTag::undefined:
        return juce::var::undefined();
    case VarTag::falseValue:
        return false;
    case VarTag::trueValue:
        return true;
    case VarTag::intValue:
    {
        const auto value = readVarInt();
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        {
            ok = false;
            return {};
        }
        return static_cast<int>(value);
    }
    case VarTag::int64Value:
        return static_cast<juce::int64>(readVarInt());
    case VarTag::doubleValue:
        return readDouble();
    case VarTag::stringValue:
        return readString();
    case VarTag::binaryValue:
        return readBlock();
    case VarTag::arrayValue:
    {
        const auto count = readCount();
        juce::Array<juce::var> elements;
        for (std::size_t index = 0; index < count && ok; ++index)
            elements.add(readVar(depth + 1));
        return elements;
    }
    }

    ok = false;
    return {};
}

juce::ValueTree Reader::readTree()
{
    return readTree(0);
}

juce::ValueTree Reader::readTree(int depth)
{
    if (depth > maximumDepth)
    {
        ok = false;
        return {};
    }

    const auto type = readString();
    if (!ok || type.isEmpty())
    {
        ok = false;
        return {};
    }

    juce::ValueTree tree{juce::Identifier(type)};
    const auto properties = readCount(2);
    for (std::size_t index = 0; index < properties && ok; ++index)
    {
        const auto name = readString();
        const auto value = readVar();
        if (name.isEmpty())
            ok = false;
        if (ok)
            tree.setProperty(juce::Identifier(name), value, nullptr);
    }

    const auto children = readCount(2);
    for (std::size_t index = 0; index < children && ok; ++index)
    {
        auto child = readTree(depth + 1);
        if (ok)
            tree.appendChild(child, nullptr);
    }

    return ok ? tree : juce::ValueTree();
}

model::NodePath Reader::readPath()
{
    model::NodePath path;
    const auto count = readCount(3);
    for (std::size_t index = 0; index < count && ok; ++index)
    {
        model::PathStep step;
        const auto type = readString();
        if (type.isEmpty())
        {
            ok = false;
            break;
        }
        step.type = juce::Identifier(type);
        switch (readByte())
        {
        case 0:
            step.key = model::PathStep::Key::id;
            step.id = readVarInt();
            break;
        case 1:
            step.key = model::PathStep::Key::target;
            step.target = readString();
            break;
        case 2:
        {
            step.key = model::PathStep::Key::index;
            const auto stepIndex = readVarUInt();
            if (stepIndex > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                ok = false;
            step.index = static_cast<int>(stepIndex);
            break;
        }
        default:
            ok = false;
        }
        path.push_back(std::move(step));
    }
    return path;
}

model::ProjectChange Reader::readChange()
{
    model::ProjectChange change;
    const auto kind = readByte();
    if (kind > static_cast<std::uint8_t>(model::ProjectChange::Kind::moveChild))
    {
        ok = false;
        return change;
    }
    change.kind = static_cast<model::ProjectChange::Kind>(kind);
    change.path = readPath();

    const auto readIndex = [this]
    {
        const auto value = readVarUInt();
        if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            ok = false;
        return static_cast<int>(value);
    };

    switch (change.kind)
    {
    case model::ProjectChange::Kind::setProperty:
    {
        const auto property = readString();
        if (property.isEmpty())
        {
            ok = false;
            return change;
        }
        change.property = juce::Identifier(property);
        if (readBool())
            change.before = readVar();

        switch (static_cast<AfterTag>(readByte()))
        {
        case AfterTag::absent:
            break;
        case AfterTag::value:
            change.after = readVar();
            break;
        case AfterTag::binaryDelta:
            if (change.before && change.before->isBinaryData())
            {
                if (auto after = readDelta(*this, *change.before->getBinaryData()))
                    change.after = juce::var(std::move(*after));
                else
                    ok = false;
            }
            else
            {
                ok = false;
            }
            break;
        case AfterTag::stringDelta:
            if (change.before && change.before->isString())
            {
                const auto after = readDelta(*this, utf8Of(change.before->toString()));
                const auto* text = after ? static_cast<const char*>(after->getData()) : nullptr;
                const auto length = after ? static_cast<int>(after->getSize()) : 0;
                if (after && (length == 0 || juce::CharPointer_UTF8::isValidString(text, length)))
                    change.after = length == 0 ? juce::String() : juce::String::fromUTF8(text, length);
                else
                    ok = false;
            }
            else
            {
                ok = false;
            }
            break;
        default:
            ok = false;
        }
        break;
    }
    case model::ProjectChange::Kind::insertChild:
    case model::ProjectChange::Kind::removeChild:
        change.index = readIndex();
        change.child = readTree();
        break;
    case model::ProjectChange::Kind::moveChild:
        change.index = readIndex();
        change.newIndex = readIndex();
        break;
    }

    return change;
}

model::ChangeSet Reader::readChanges()
{
    model::ChangeSet changes;
    const auto count = readCount(2);
    changes.reserve(count);
    for (std::size_t index = 0; index < count && ok; ++index)
        changes.push_back(readChange());
    return changes;
}

} // namespace daw::coop
