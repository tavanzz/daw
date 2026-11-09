#pragma once

#include <juce_core/juce_core.h>

namespace daw::services
{

/**
    Ed25519 signatures (RFC 8032), by Monocypher. The app only verifies: releases are
    signed by the release workflow with a private key the app never sees, and the app
    carries the public key (updatePublicKey()).
*/
namespace ed25519
{

inline constexpr size_t seedSize = 32;
inline constexpr size_t publicKeySize = 32;
inline constexpr size_t secretKeySize = 64; ///< Monocypher's: the seed then the public key
inline constexpr size_t signatureSize = 64;

/** True only if `signature` is `publicKey`'s signature of `message`. */
[[nodiscard]] bool verify(const juce::MemoryBlock& message, const juce::MemoryBlock& signature,
                          const juce::MemoryBlock& publicKey);

/** The key pair for a random seed: secret key (64 bytes), public key (32 bytes). */
struct KeyPair
{
    juce::MemoryBlock secretKey, publicKey;
};
[[nodiscard]] KeyPair keyPairFromSeed(const juce::MemoryBlock& seed);

/** `secretKey`'s signature of `message` (64 bytes). */
[[nodiscard]] juce::MemoryBlock sign(const juce::MemoryBlock& message, const juce::MemoryBlock& secretKey);

} // namespace ed25519

/** The public key this build trusts for updates (empty if it was built without one). */
[[nodiscard]] juce::MemoryBlock updatePublicKey();

/** SHA-256 of a file, as lower-case hex (empty if it cannot be read). */
[[nodiscard]] juce::String sha256Of(const juce::File& file);

} // namespace daw::services
