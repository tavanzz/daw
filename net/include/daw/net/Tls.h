#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace daw::net
{

/** Makes Mbed TLS ready (thread safety, the cryptography subsystem). Called by everything
    in this module that needs it; safe from any thread, any number of times. */
void initialiseTls();

/**
    A server's identity: an ECDSA P-256 key pair and a self-signed certificate for it, in
    PEM. Clients do not check it against certificate authorities: they compare the
    fingerprint of its public key with the one the invite link gave them (pinning), so a
    server needs no domain name and no renewals.
*/
struct ServerIdentity
{
    juce::String certificatePem;
    juce::String privateKeyPem;

    /** A new key pair and certificate (valid for a hundred years: pinned, never expired). */
    [[nodiscard]] static std::optional<ServerIdentity> create();

    /** The identity kept in folder (certificate.pem, private-key.pem), made and saved
        there the first time (the key readable by its owner only). */
    [[nodiscard]] static std::optional<ServerIdentity> loadOrCreate(const juce::File& folder);

    /** The fingerprint of the certificate's public key; empty if it is not valid. */
    [[nodiscard]] juce::String getFingerprint() const;
};

/** SHA-256 of a certificate's public key (its DER SubjectPublicKeyInfo), in Base64url
    without padding: 43 characters, short enough for links. Empty if pem is not a
    certificate. */
[[nodiscard]] juce::String fingerprintOfCertificate(const juce::String& certificatePem);

/** SHA-256 of bytes (with Mbed TLS: hardware accelerated where the processor can). */
[[nodiscard]] juce::MemoryBlock sha256(const void* data, std::size_t size);

/** Incremental SHA-256, for files read in pieces. */
class Sha256
{
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void add(const void* data, std::size_t size);
    [[nodiscard]] juce::MemoryBlock finish();

private:
    struct State;
    std::unique_ptr<State> state;
};

/** Bytes from the system's cryptographic random source (through PSA); false if there is
    none. For tokens, codes and ids nobody may guess. */
[[nodiscard]] bool secureRandom(void* destination, std::size_t size);

/** Base64url without padding (RFC 4648, section 5), and back (nothing if not valid). */
[[nodiscard]] juce::String toBase64Url(const void* data, std::size_t size);
[[nodiscard]] std::optional<juce::MemoryBlock> fromBase64Url(const juce::String& text);

} // namespace daw::net
