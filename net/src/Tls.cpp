#include "daw/net/Tls.h"

#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#if !JUCE_WINDOWS
 #include <sys/stat.h>
#endif

#include <array>
#include <mutex>

namespace daw::net
{

namespace detail
{
void installTlsThreading();
}

namespace
{

constexpr const char* certificateFileName = "certificate.pem";
constexpr const char* privateKeyFileName = "private-key.pem";

// PEM text with the terminating null Mbed TLS's parsers want.
std::vector<unsigned char> pemBytes(const juce::String& pem)
{
    const auto utf8 = pem.toUTF8();
    return {utf8.getAddress(), utf8.getAddress() + utf8.sizeInBytes()};
}

struct Pk
{
    Pk() { mbedtls_pk_init(&context); }
    ~Pk() { mbedtls_pk_free(&context); }
    Pk(const Pk&) = delete;
    Pk& operator=(const Pk&) = delete;

    mbedtls_pk_context context;
};

struct Certificate
{
    Certificate() { mbedtls_x509_crt_init(&context); }
    ~Certificate() { mbedtls_x509_crt_free(&context); }
    Certificate(const Certificate&) = delete;
    Certificate& operator=(const Certificate&) = delete;

    mbedtls_x509_crt context;
};

struct CertificateWriter
{
    CertificateWriter() { mbedtls_x509write_crt_init(&context); }
    ~CertificateWriter() { mbedtls_x509write_crt_free(&context); }
    CertificateWriter(const CertificateWriter&) = delete;
    CertificateWriter& operator=(const CertificateWriter&) = delete;

    mbedtls_x509write_cert context;
};

bool generateKey(Pk& pk)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));

    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    if (psa_generate_key(&attributes, &key) != PSA_SUCCESS)
        return false;

    const bool copied = mbedtls_pk_copy_from_psa(key, &pk.context) == 0;
    psa_destroy_key(key);
    return copied;
}

bool writePrivately(const juce::File& file, const juce::String& text)
{
    if (!file.create())
        return false;
#if !JUCE_WINDOWS
    chmod(file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
#endif
    return file.replaceWithText(text, false, false, "\n");
}

} // namespace

void initialiseTls()
{
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       detail::installTlsThreading();
                       [[maybe_unused]] const auto status = psa_crypto_init();
                       jassert(status == PSA_SUCCESS);
                   });
}

std::optional<ServerIdentity> ServerIdentity::create()
{
    initialiseTls();

    Pk key;
    if (!generateKey(key))
        return std::nullopt;

    CertificateWriter writer;
    auto& certificate = writer.context;
    mbedtls_x509write_crt_set_version(&certificate, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&certificate, &key.context);
    mbedtls_x509write_crt_set_issuer_key(&certificate, &key.context);

    std::array<unsigned char, 16> serial{};
    if (psa_generate_random(serial.data(), serial.size()) != PSA_SUCCESS)
        return std::nullopt;
    serial[0] &= 0x7f; // a positive number
    serial[0] |= 0x01; // without a leading zero byte

    if (mbedtls_x509write_crt_set_serial_raw(&certificate, serial.data(), serial.size()) != 0
        || mbedtls_x509write_crt_set_subject_name(&certificate, "CN=MICINSTATION co-op server") != 0
        || mbedtls_x509write_crt_set_issuer_name(&certificate, "CN=MICINSTATION co-op server") != 0
        || mbedtls_x509write_crt_set_validity(&certificate, "20260101000000", "21251231235959") != 0
        || mbedtls_x509write_crt_set_basic_constraints(&certificate, 0, -1) != 0)
        return std::nullopt;

    std::vector<unsigned char> buffer(8192);
    if (mbedtls_x509write_crt_pem(&certificate, buffer.data(), buffer.size()) != 0)
        return std::nullopt;
    ServerIdentity identity;
    identity.certificatePem = juce::String(reinterpret_cast<const char*>(buffer.data()));

    std::fill(buffer.begin(), buffer.end(), 0);
    if (mbedtls_pk_write_key_pem(&key.context, buffer.data(), buffer.size()) != 0)
        return std::nullopt;
    identity.privateKeyPem = juce::String(reinterpret_cast<const char*>(buffer.data()));
    std::fill(buffer.begin(), buffer.end(), 0);

    return identity;
}

std::optional<ServerIdentity> ServerIdentity::loadOrCreate(const juce::File& folder)
{
    const auto certificateFile = folder.getChildFile(certificateFileName);
    const auto keyFile = folder.getChildFile(privateKeyFileName);

    if (certificateFile.existsAsFile() && keyFile.existsAsFile())
    {
        ServerIdentity identity{certificateFile.loadFileAsString(), keyFile.loadFileAsString()};
        if (identity.getFingerprint().isEmpty())
            return std::nullopt;
        return identity;
    }

    auto identity = create();
    if (!identity || !folder.createDirectory()
        || !writePrivately(keyFile, identity->privateKeyPem)
        || !certificateFile.replaceWithText(identity->certificatePem, false, false, "\n"))
        return std::nullopt;
    return identity;
}

juce::String ServerIdentity::getFingerprint() const
{
    return fingerprintOfCertificate(certificatePem);
}

juce::String fingerprintOfCertificate(const juce::String& certificatePem)
{
    initialiseTls();

    Certificate certificate;
    const auto bytes = pemBytes(certificatePem);
    if (mbedtls_x509_crt_parse(&certificate.context, bytes.data(), bytes.size()) != 0)
        return {};

    const auto hash = sha256(certificate.context.pk_raw.p, certificate.context.pk_raw.len);
    return toBase64Url(hash.getData(), hash.getSize());
}

juce::MemoryBlock sha256(const void* data, std::size_t size)
{
    Sha256 hasher;
    hasher.add(data, size);
    return hasher.finish();
}

struct Sha256::State
{
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
};

Sha256::Sha256()
    : state(std::make_unique<State>())
{
    initialiseTls();
    [[maybe_unused]] const auto status = psa_hash_setup(&state->operation, PSA_ALG_SHA_256);
    jassert(status == PSA_SUCCESS);
}

Sha256::~Sha256()
{
    psa_hash_abort(&state->operation);
}

void Sha256::add(const void* data, std::size_t size)
{
    if (size > 0)
        psa_hash_update(&state->operation, static_cast<const std::uint8_t*>(data), size);
}

juce::MemoryBlock Sha256::finish()
{
    juce::MemoryBlock hash(32);
    std::size_t length = 0;
    if (psa_hash_finish(&state->operation, static_cast<std::uint8_t*>(hash.getData()),
                        hash.getSize(), &length)
            != PSA_SUCCESS
        || length != 32)
        return {};
    return hash;
}

bool secureRandom(void* destination, std::size_t size)
{
    initialiseTls();
    return psa_generate_random(static_cast<std::uint8_t*>(destination), size) == PSA_SUCCESS;
}

juce::String toBase64Url(const void* data, std::size_t size)
{
    return juce::Base64::toBase64(data, size)
        .replaceCharacter('+', '-')
        .replaceCharacter('/', '_')
        .trimCharactersAtEnd("=");
}

std::optional<juce::MemoryBlock> fromBase64Url(const juce::String& text)
{
    if (!text.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")
        || text.length() % 4 == 1)
        return std::nullopt;

    auto standard = text.replaceCharacter('-', '+').replaceCharacter('_', '/');
    while (standard.length() % 4 != 0)
        standard << '=';

    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, standard))
        return std::nullopt;
    return decoded.getMemoryBlock();
}

} // namespace daw::net
