#include "daw/services/Ed25519.h"

#include <juce_cryptography/juce_cryptography.h>

#include <monocypher-ed25519.h>
#include <monocypher.h>

namespace daw::services
{

namespace ed25519
{

namespace
{

const uint8_t* bytes(const juce::MemoryBlock& block)
{
    return static_cast<const uint8_t*>(block.getData());
}

uint8_t* bytes(juce::MemoryBlock& block)
{
    return static_cast<uint8_t*>(block.getData());
}

} // namespace

bool verify(const juce::MemoryBlock& message, const juce::MemoryBlock& signature,
            const juce::MemoryBlock& publicKey)
{
    if (signature.getSize() != signatureSize || publicKey.getSize() != publicKeySize)
        return false;
    return crypto_ed25519_check(bytes(signature), bytes(publicKey), bytes(message), message.getSize()) == 0;
}

KeyPair keyPairFromSeed(const juce::MemoryBlock& seed)
{
    jassert(seed.getSize() == seedSize);
    KeyPair pair;
    pair.secretKey.setSize(secretKeySize, true);
    pair.publicKey.setSize(publicKeySize, true);
    juce::MemoryBlock seedCopy(seed); // Monocypher wipes the seed it is given
    seedCopy.setSize(seedSize, true);
    crypto_ed25519_key_pair(bytes(pair.secretKey), bytes(pair.publicKey), bytes(seedCopy));
    return pair;
}

juce::MemoryBlock sign(const juce::MemoryBlock& message, const juce::MemoryBlock& secretKey)
{
    juce::MemoryBlock signature(signatureSize, true);
    if (secretKey.getSize() == secretKeySize)
        crypto_ed25519_sign(bytes(signature), bytes(secretKey), bytes(message), message.getSize());
    return signature;
}

} // namespace ed25519

juce::MemoryBlock updatePublicKey()
{
    juce::MemoryOutputStream key;
    if (!juce::Base64::convertFromBase64(key, DAW_UPDATE_PUBLIC_KEY) || key.getDataSize() != ed25519::publicKeySize)
        return {};
    return key.getMemoryBlock();
}

juce::String sha256Of(const juce::File& file)
{
    juce::FileInputStream stream(file);
    if (!stream.openedOk())
        return {};
    return juce::SHA256(stream).toHexString();
}

} // namespace daw::services
