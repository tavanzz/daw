#include "daw/services/Ed25519.h"
#include "daw/services/SemanticVersion.h"
#include "daw/services/UpdateManifest.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

using daw::services::SemanticVersion;
namespace ed25519 = daw::services::ed25519;
namespace manifest = daw::services::manifest;

namespace
{

juce::MemoryBlock fromHex(const juce::String& hex)
{
    juce::MemoryBlock block;
    block.loadFromHexString(hex);
    return block;
}

juce::String toHex(const juce::MemoryBlock& block)
{
    return juce::String::toHexString(block.getData(), static_cast<int>(block.getSize()), 0);
}

SemanticVersion version(const char* text)
{
    const auto parsed = SemanticVersion::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

ed25519::KeyPair testKeys(int seedByte)
{
    juce::MemoryBlock seed(ed25519::seedSize);
    seed.fillWith(static_cast<juce::uint8>(seedByte));
    return ed25519::keyPairFromSeed(seed);
}

juce::String payloadFor(const juce::String& versionText, const juce::String& assetUrl = "https://x/DAW.dmg")
{
    return R"({"bundleId": "io.github.tavanzz.daw", "version": ")" + versionText +
           R"(", "date": "2026-11-20", "notes": "- Faster\n- Fewer bugs", "releaseUrl": "https://x/r",
              "assets": {"macos": {"url": ")" + assetUrl +
           R"(", "size": 3, "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}}})";
}

} // namespace

TEST_CASE("Ed25519 matches the RFC 8032 test vectors", "[services][updates]")
{
    // RFC 8032, section 7.1, tests 1 and 2.
    const auto pair1 = ed25519::keyPairFromSeed(fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"));
    CHECK(toHex(pair1.publicKey) == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const auto signature1 = ed25519::sign({}, pair1.secretKey);
    CHECK(toHex(signature1) == "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bac"
                               "c61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    CHECK(ed25519::verify({}, signature1, pair1.publicKey));

    const auto pair2 = ed25519::keyPairFromSeed(fromHex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb"));
    CHECK(toHex(pair2.publicKey) == "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
    const auto message2 = fromHex("72");
    const auto signature2 = ed25519::sign(message2, pair2.secretKey);
    CHECK(toHex(signature2) == "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e"
                               "458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
    CHECK(ed25519::verify(message2, signature2, pair2.publicKey));

    // Another message, key or a flipped bit fails.
    CHECK_FALSE(ed25519::verify(fromHex("73"), signature2, pair2.publicKey));
    CHECK_FALSE(ed25519::verify(message2, signature2, pair1.publicKey));
    auto flipped = signature2;
    auto* flippedBytes = static_cast<juce::uint8*>(flipped.getData());
    flippedBytes[10] = static_cast<juce::uint8>(flippedBytes[10] ^ 1);
    CHECK_FALSE(ed25519::verify(message2, flipped, pair2.publicKey));
    CHECK_FALSE(ed25519::verify(message2, juce::MemoryBlock(10), pair2.publicKey));
}

TEST_CASE("Versions are ordered as Semantic Versioning says", "[services][updates]")
{
    CHECK(version("0.8.0-rc.1") < version("0.8.0-rc.2"));
    CHECK(version("0.8.0-rc.2") < version("0.8.0-rc.10"));
    CHECK(version("0.8.0-rc.10") < version("0.8.0"));
    CHECK(version("0.8.0") < version("0.8.1"));
    CHECK(version("0.8.1") < version("0.10.0"));
    CHECK(version("0.10.0") < version("1.0.0"));
    CHECK(version("1.0.0-alpha") < version("1.0.0-alpha.1"));
    CHECK(version("1.0.0-alpha.1") < version("1.0.0-alpha.beta"));
    CHECK(version("1.0.0-beta.2") < version("1.0.0-beta.11"));
    CHECK(version("v1.2.3") == version("1.2.3+build.7"));
    CHECK(version("v0.8.0-rc.1").toString() == "0.8.0-rc.1");
    CHECK(version("0.8.0-rc.1").isPreRelease());
    CHECK_FALSE(version("0.8.0").isPreRelease());

    for (const auto* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.x", "1.2.3-", "1.2.3-rc..1", "1.2.3-rc.01", "-1.2.3"})
        CHECK_FALSE(SemanticVersion::parse(bad).has_value());
}

TEST_CASE("A signed manifest is read only with the right key and an untouched payload",
          "[services][updates]")
{
    const auto keys = testKeys(7);
    const auto signedManifest = manifest::sign(payloadFor("0.9.0"), keys.secretKey);

    const auto read = manifest::read(signedManifest, keys.publicKey);
    REQUIRE(read.release.has_value());
    CHECK(read.problem.isEmpty());
    CHECK(read.release->bundleId == "io.github.tavanzz.daw");
    CHECK(read.release->version == version("0.9.0"));
    CHECK(read.release->notes == "- Faster\n- Fewer bugs");
    const auto* asset = read.release->assetFor("macos");
    REQUIRE(asset != nullptr);
    CHECK(asset->size == 3);
    CHECK(read.release->assetFor("windows-x86_64") == nullptr);

    // Another key.
    const auto other = manifest::read(signedManifest, testKeys(8).publicKey);
    CHECK_FALSE(other.release.has_value());
    CHECK(other.problem.contains("signature"));

    // A payload changed after signing (another version, another download address).
    auto json = juce::JSON::parse(signedManifest);
    const auto tampered = payloadFor("9.9.9", "https://elsewhere/DAW.dmg");
    json.getDynamicObject()->setProperty("payload",
                                         juce::Base64::toBase64(tampered.toRawUTF8(), tampered.getNumBytesAsUTF8()));
    const auto changed = manifest::read(juce::JSON::toString(json), keys.publicKey);
    CHECK_FALSE(changed.release.has_value());
    CHECK(changed.problem.contains("signature"));

    // No key in the build, or nonsense.
    CHECK(manifest::read(signedManifest, {}).problem.contains("no key"));
    CHECK_FALSE(manifest::read("<html>Not found</html>", keys.publicKey).release.has_value());
    CHECK_FALSE(manifest::read(R"({"format": 2})", keys.publicKey).release.has_value());
}

TEST_CASE("A downloaded file must have the manifest's size and SHA-256", "[services][updates]")
{
    daw::test::TemporaryDirectory directory;
    const auto file = directory.file("DAW.dmg");
    REQUIRE(file.replaceWithText("abc"));
    CHECK(daw::services::sha256Of(file) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const auto keys = testKeys(1);
    const auto release = manifest::read(manifest::sign(payloadFor("1.0.0"), keys.secretKey), keys.publicKey).release;
    REQUIRE(release.has_value());
    CHECK(manifest::fileMatches(file, *release->assetFor("macos")));

    REQUIRE(file.replaceWithText("abd"));
    CHECK_FALSE(manifest::fileMatches(file, *release->assetFor("macos")));
    REQUIRE(file.replaceWithText("abcd"));
    CHECK_FALSE(manifest::fileMatches(file, *release->assetFor("macos")));
    CHECK_FALSE(manifest::fileMatches(directory.file("missing"), *release->assetFor("macos")));
}
