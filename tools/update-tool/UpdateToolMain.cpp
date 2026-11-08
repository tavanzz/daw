// daw_update_tool: makes and checks the keys and manifests of signed updates
// (services/UpdateManifest.h). The release workflow signs each release's update.json
// with it; the maintainer makes the key pair once with "keygen".
//
//   daw_update_tool keygen <private key file>    new key pair; prints the public key
//   daw_update_tool public-key <private key file>
//   daw_update_tool sign <payload.json> <private key file> <update.json>
//   daw_update_tool verify <update.json> <public key, Base64>
//   daw_update_tool sha256 <file>
//
// Key files hold the Base64 of Monocypher's 64-byte Ed25519 secret key.

#include "daw/services/Ed25519.h"
#include "daw/services/UpdateManifest.h"

#include <juce_core/juce_core.h>

#include <cstdio>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <bcrypt.h>
 #pragma comment(lib, "bcrypt.lib")
#elif JUCE_MAC
 #include <Security/SecRandom.h>
#else
 #include <sys/random.h>
#endif

#if !JUCE_WINDOWS
 #include <sys/stat.h>
#endif

namespace
{

namespace ed25519 = daw::services::ed25519;

int fail(const juce::String& message)
{
    std::fputs(("daw_update_tool: " + message + "\n").toRawUTF8(), stderr);
    return 1;
}

void print(const juce::String& text)
{
    std::fputs((text + "\n").toRawUTF8(), stdout);
}

// The seed comes from the system's cryptographic random source, never juce::Random.
bool secureRandom(void* buffer, size_t size)
{
#if JUCE_WINDOWS
    return BCryptGenRandom(nullptr, static_cast<PUCHAR>(buffer), static_cast<ULONG>(size),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif JUCE_MAC
    return SecRandomCopyBytes(kSecRandomDefault, size, buffer) == errSecSuccess;
#else
    return getrandom(buffer, size, 0) == static_cast<ssize_t>(size);
#endif
}

std::optional<juce::MemoryBlock> readBase64(const juce::String& text, size_t expectedSize)
{
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, text.trim()) || decoded.getDataSize() != expectedSize)
        return std::nullopt;
    return decoded.getMemoryBlock();
}

std::optional<juce::MemoryBlock> readSecretKey(const juce::File& file)
{
    return readBase64(file.loadFileAsString(), ed25519::secretKeySize);
}

juce::String base64(const juce::MemoryBlock& block)
{
    return juce::Base64::toBase64(block.getData(), block.getSize());
}

juce::File fileArgument(const juce::String& path)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile(path);
}

int keygen(const juce::File& keyFile)
{
    if (keyFile.exists())
        return fail(keyFile.getFullPathName() + " exists: not overwritten");

    juce::MemoryBlock seed(ed25519::seedSize);
    if (!secureRandom(seed.getData(), seed.getSize()))
        return fail("no random numbers from the system");
    const auto pair = ed25519::keyPairFromSeed(seed);
    seed.fillWith(0);

    // Readable by its owner only, before the key goes in.
    if (!keyFile.create())
        return fail("cannot write " + keyFile.getFullPathName());
#if !JUCE_WINDOWS
    chmod(keyFile.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
#endif
    {
        juce::FileOutputStream stream(keyFile);
        if (stream.failedToOpen() || !stream.writeText(base64(pair.secretKey) + "\n", false, false, "\n"))
            return fail("cannot write " + keyFile.getFullPathName());
    }
    print(base64(pair.publicKey));
    return 0;
}

int publicKey(const juce::File& keyFile)
{
    const auto secret = readSecretKey(keyFile);
    if (!secret)
        return fail("not a private key file: " + keyFile.getFullPathName());
    // Monocypher's secret key is the seed followed by the public key.
    print(base64(juce::MemoryBlock(static_cast<const char*>(secret->getData()) + ed25519::seedSize,
                                   ed25519::publicKeySize)));
    return 0;
}

int sign(const juce::File& payloadFile, const juce::File& keyFile, const juce::File& output)
{
    const auto secret = readSecretKey(keyFile);
    if (!secret)
        return fail("not a private key file: " + keyFile.getFullPathName());
    const auto payload = payloadFile.loadFileAsString();
    if (!juce::JSON::parse(payload).isObject())
        return fail("the payload is not a JSON object: " + payloadFile.getFullPathName());
    if (!output.replaceWithText(daw::services::manifest::sign(payload, *secret)))
        return fail("cannot write " + output.getFullPathName());
    return 0;
}

int verify(const juce::File& manifestFile, const juce::String& publicKeyText)
{
    const auto key = readBase64(publicKeyText, ed25519::publicKeySize);
    if (!key)
        return fail("not a public key: " + publicKeyText);
    const auto read = daw::services::manifest::read(manifestFile.loadFileAsString(), *key);
    if (!read.release)
        return fail(read.problem);
    print("valid: " + read.release->bundleId + " " + read.release->version.toString() + ", " +
          juce::String(static_cast<int>(read.release->assets.size())) + " assets");
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    juce::StringArray args;
    for (int index = 1; index < argc; ++index)
        args.add(juce::CharPointer_UTF8(argv[index]));

    const auto command = args[0];
    if (command == "keygen" && args.size() == 2)
        return keygen(fileArgument(args[1]));
    if (command == "public-key" && args.size() == 2)
        return publicKey(fileArgument(args[1]));
    if (command == "sign" && args.size() == 4)
        return sign(fileArgument(args[1]), fileArgument(args[2]), fileArgument(args[3]));
    if (command == "verify" && args.size() == 3)
        return verify(fileArgument(args[1]), args[2]);
    if (command == "sha256" && args.size() == 2)
    {
        const auto hash = daw::services::sha256Of(fileArgument(args[1]));
        if (hash.isEmpty())
            return fail("cannot read " + args[1]);
        print(hash);
        return 0;
    }

    return fail("usage: keygen <key file> | public-key <key file> | sign <payload.json> <key file> "
                "<update.json> | verify <update.json> <public key> | sha256 <file>");
}
