#include "daw/services/UpdateManifest.h"

#include "daw/services/Ed25519.h"

namespace daw::services
{

const UpdateAsset* UpdateRelease::assetFor(const juce::String& platform) const
{
    const auto found = assets.find(platform);
    return found != assets.end() ? &found->second : nullptr;
}

namespace manifest
{

namespace
{

std::optional<juce::MemoryBlock> fromBase64(const juce::String& text)
{
    juce::MemoryOutputStream decoded;
    if (text.isEmpty() || !juce::Base64::convertFromBase64(decoded, text))
        return std::nullopt;
    return decoded.getMemoryBlock();
}

bool isHexDigest(const juce::String& text)
{
    return text.length() == 64 && text.containsOnly("0123456789abcdef");
}

} // namespace

juce::String platformKey()
{
#if JUCE_MAC
    return "macos";
#elif JUCE_WINDOWS
    return "windows-x86_64";
#elif JUCE_LINUX
    return "linux-x86_64";
#else
    return "unknown";
#endif
}

ReadResult read(const juce::String& manifestJson, const juce::MemoryBlock& publicKey)
{
    const auto failure = [](const juce::String& problem) { return ReadResult{std::nullopt, problem}; };

    const auto outer = juce::JSON::parse(manifestJson);
    if (!outer.isObject())
        return failure("the update information is not readable");
    if (static_cast<int>(outer["format"]) != format)
        return failure("the update information has a format this version does not know");

    const auto payload = fromBase64(outer["payload"].toString());
    const auto signature = fromBase64(outer["signature"].toString());
    if (!payload || !signature)
        return failure("the update information is not readable");

    // Nothing in the payload is read before its signature is checked.
    if (publicKey.getSize() != ed25519::publicKeySize)
        return failure("this build has no key to check updates with");
    if (!ed25519::verify(*payload, *signature, publicKey))
        return failure("the update's signature is not valid");

    const auto json = juce::JSON::parse(payload->toString());
    UpdateRelease release;
    release.bundleId = json["bundleId"].toString();
    const auto version = SemanticVersion::parse(json["version"].toString());
    if (!json.isObject() || !version || release.bundleId.isEmpty())
        return failure("the update information is incomplete");
    release.version = *version;
    release.date = json["date"].toString();
    release.notes = json["notes"].toString();
    release.releaseUrl = json["releaseUrl"].toString();

    if (const auto* assets = json["assets"].getDynamicObject())
        for (const auto& property : assets->getProperties())
        {
            UpdateAsset asset;
            asset.url = property.value["url"].toString();
            asset.size = static_cast<juce::int64>(property.value["size"]);
            asset.sha256 = property.value["sha256"].toString().toLowerCase();
            if (asset.url.isEmpty() || asset.size <= 0 || !isHexDigest(asset.sha256))
                return failure("the update information is incomplete");
            release.assets[property.name.toString()] = asset;
        }

    return {release, {}};
}

juce::String sign(const juce::String& payloadJson, const juce::MemoryBlock& secretKey)
{
    const juce::MemoryBlock payload(payloadJson.toRawUTF8(), payloadJson.getNumBytesAsUTF8());
    auto outer = std::make_unique<juce::DynamicObject>();
    outer->setProperty("format", format);
    outer->setProperty("payload", juce::Base64::toBase64(payload.getData(), payload.getSize()));
    const auto signature = ed25519::sign(payload, secretKey);
    outer->setProperty("signature", juce::Base64::toBase64(signature.getData(), signature.getSize()));
    return juce::JSON::toString(juce::var(outer.release()), false) + "\n";
}

bool fileMatches(const juce::File& file, const UpdateAsset& asset)
{
    return file.existsAsFile() && file.getSize() == asset.size && sha256Of(file) == asset.sha256;
}

} // namespace manifest

} // namespace daw::services
