#include "FileStore.h"

namespace daw::server
{

namespace
{

juce::String hexOf(const juce::MemoryBlock& hash)
{
    return juce::String::toHexString(hash.getData(), static_cast<int>(hash.getSize()), 0);
}

bool isHex(const juce::String& text)
{
    return text.length() == 64 && text.containsOnly("0123456789abcdef");
}

} // namespace

FileStore::FileStore(juce::File storeFolder, std::uint64_t maximum, std::uint64_t maximumFile)
    : folder(std::move(storeFolder)), maximumBytes(maximum), maximumFileBytes(maximumFile)
{
    folder.createDirectory();
    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
    {
        const auto file = entry.getFile();
        const auto name = file.getFileName();
        const auto size = static_cast<std::uint64_t>(file.getSize());
        if (isHex(name))
        {
            complete[name] = size;
            bytes += size;
        }
        else if (name.endsWith(".part") && isHex(name.upToFirstOccurrenceOf(".", false, false)))
        {
            Upload upload;
            upload.size = static_cast<std::uint64_t>(
                name.fromFirstOccurrenceOf(".", false, false).upToFirstOccurrenceOf(".", false, false)
                    .getLargeIntValue());
            upload.stored = size;
            if (upload.stored > upload.size)
            {
                file.deleteFile();
                continue;
            }
            uploads[name.upToFirstOccurrenceOf(".", false, false)] = std::move(upload);
            bytes += size;
        }
    }
}

juce::File FileStore::completeFile(const juce::String& hex) const
{
    return folder.getChildFile(hex);
}

juce::File FileStore::partFile(const juce::String& hex, std::uint64_t size) const
{
    return folder.getChildFile(hex + "." + juce::String(static_cast<juce::int64>(size)) + ".part");
}

FileStore::Status FileStore::query(const juce::MemoryBlock& hash, std::uint64_t size,
                                   std::uint64_t serverRoom)
{
    const auto hex = hexOf(hash);
    if (const auto found = complete.find(hex); found != complete.end())
        return {found->second, true, false};

    if (const auto found = uploads.find(hex); found != uploads.end())
    {
        if (found->second.size == size)
            return {found->second.stored, false, false};
        // Another size for the same hash: one of them lies; start again.
        partFile(hex, found->second.size).deleteFile();
        bytes -= found->second.stored;
        uploads.erase(found);
    }

    if (size == 0 || size > maximumFileBytes || bytes + size > maximumBytes || size > serverRoom)
        return {0, false, true};

    Upload upload;
    upload.size = size;
    if (!partFile(hex, size).create())
        return {0, false, true};
    uploads[hex] = std::move(upload);
    return {0, false, false};
}

FileStore::Status FileStore::write(const juce::MemoryBlock& hash, std::uint64_t offset,
                                   const juce::MemoryBlock& data, bool& completedNow)
{
    completedNow = false;
    const auto hex = hexOf(hash);
    if (const auto found = complete.find(hex); found != complete.end())
        return {found->second, true, false};

    const auto found = uploads.find(hex);
    if (found == uploads.end())
        return {0, false, false};
    auto& upload = found->second;
    const auto part = partFile(hex, upload.size);

    if (offset != upload.stored || data.isEmpty() || upload.stored + data.getSize() > upload.size)
        return {upload.stored, false, false};

    if (upload.hasher == nullptr)
    {
        // After a restart: what is there is hashed once.
        upload.hasher = std::make_unique<net::Sha256>();
        juce::FileInputStream input(part);
        juce::HeapBlock<char> buffer(1 << 16);
        for (std::uint64_t done = 0; done < upload.stored;)
        {
            const auto read = input.read(buffer.get(), 1 << 16);
            if (read <= 0)
                break;
            upload.hasher->add(buffer.get(), static_cast<std::size_t>(read));
            done += static_cast<std::uint64_t>(read);
        }
    }

    {
        juce::FileOutputStream output(part);
        if (output.failedToOpen() || !output.write(data.getData(), data.getSize()))
            return {upload.stored, false, false};
    }
    upload.hasher->add(data.getData(), data.getSize());
    upload.stored += data.getSize();
    bytes += data.getSize();

    if (upload.stored < upload.size)
        return {upload.stored, false, false};

    const auto size = upload.size;
    const bool matches = upload.hasher->finish() == hash;
    uploads.erase(found);
    if (!matches || !part.moveFileTo(completeFile(hex)))
    {
        part.deleteFile();
        bytes -= size;
        return {0, false, true};
    }
    complete[hex] = size;
    completedNow = true;
    return {size, true, false};
}

bool FileStore::has(const juce::MemoryBlock& hash) const
{
    return complete.count(hexOf(hash)) > 0;
}

std::uint64_t FileStore::sizeOf(const juce::MemoryBlock& hash) const
{
    const auto found = complete.find(hexOf(hash));
    return found != complete.end() ? found->second : 0;
}

juce::MemoryBlock FileStore::read(const juce::MemoryBlock& hash, std::uint64_t offset,
                                  std::uint64_t length) const
{
    const auto hex = hexOf(hash);
    const auto found = complete.find(hex);
    if (found == complete.end() || offset >= found->second)
        return {};
    length = std::min(length, found->second - offset);

    juce::FileInputStream input(completeFile(hex));
    if (input.failedToOpen() || !input.setPosition(static_cast<juce::int64>(offset)))
        return {};
    juce::MemoryBlock data(static_cast<std::size_t>(length));
    const auto read = input.read(data.getData(), static_cast<int>(length));
    if (read <= 0)
        return {};
    data.setSize(static_cast<std::size_t>(read));
    return data;
}

} // namespace daw::server
