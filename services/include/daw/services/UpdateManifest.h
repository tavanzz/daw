#pragma once

#include "daw/services/SemanticVersion.h"

#include <juce_core/juce_core.h>

#include <map>
#include <optional>

namespace daw::services
{

/** One release file for one platform. */
struct UpdateAsset
{
    juce::String url;     ///< https:// (file:// in tests)
    juce::int64 size = 0; ///< bytes
    juce::String sha256;  ///< lower-case hex
};

/** What a signed update manifest says about a release. */
struct UpdateRelease
{
    juce::String bundleId; ///< the app it is for
    SemanticVersion version; ///< 0.0.0 until read
    juce::String date;       ///< ISO 8601 day
    juce::String notes;      ///< Markdown, generated from the commits
    juce::String releaseUrl; ///< the release's web page
    std::map<juce::String, UpdateAsset> assets; ///< by platform key (platformKey())

    [[nodiscard]] const UpdateAsset* assetFor(const juce::String& platform) const;
};

/**
    The update manifest ("update.json", one per GitHub release):

        { "format": 1, "payload": "<Base64 of the payload JSON>", "signature": "<Base64>" }

    The signature is Ed25519 over the payload's exact bytes, so nothing depends on how
    JSON is written. The payload:

        { "bundleId": "...", "version": "0.8.1", "date": "2026-11-20", "notes": "...",
          "releaseUrl": "...", "assets": { "macos": { "url", "size", "sha256" }, ... } }
*/
namespace manifest
{

inline constexpr int format = 1;

/** The platform key of the running build: "macos" (universal), "windows-x86_64",
    "linux-x86_64". */
[[nodiscard]] juce::String platformKey();

/** Reads a manifest and checks its signature against `publicKey`. */
struct ReadResult
{
    std::optional<UpdateRelease> release;
    juce::String problem; ///< why there is no release
};
[[nodiscard]] ReadResult read(const juce::String& manifestJson, const juce::MemoryBlock& publicKey);

/** Signs a payload (the JSON text, written as it is) into a manifest. */
[[nodiscard]] juce::String sign(const juce::String& payloadJson, const juce::MemoryBlock& secretKey);

/** True if `file` is the asset: its size and SHA-256. */
[[nodiscard]] bool fileMatches(const juce::File& file, const UpdateAsset& asset);

} // namespace manifest

} // namespace daw::services
