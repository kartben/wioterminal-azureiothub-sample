#include "signature.h"
#include <string.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

std::string GenerateEncryptedSignature(const std::string& symmetricKey, const std::vector<uint8_t>& signature)
{
    unsigned char base64DecodedSymmetricKey[symmetricKey.size() + 1];

    // Base64-decode device key
    // <-- symmetricKey
    // --> base64DecodedSymmetricKey
    size_t base64DecodedSymmetricKeyLength;
    if (mbedtls_base64_decode(base64DecodedSymmetricKey, sizeof(base64DecodedSymmetricKey), &base64DecodedSymmetricKeyLength, (unsigned char*)&symmetricKey[0], symmetricKey.size()) != 0) abort();
    if (base64DecodedSymmetricKeyLength == 0) abort();

    // SHA-256 encrypt
    // <-- base64DecodedSymmetricKey
    // <-- signature
    // --> encryptedSignature
    uint8_t encryptedSignature[32]; // SHA-256
    mbedtls_md_context_t ctx;
    const mbedtls_md_type_t mdType{ MBEDTLS_MD_SHA256 };
    if (mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(mdType), 1) != 0) abort();
    if (mbedtls_md_hmac_starts(&ctx, base64DecodedSymmetricKey, base64DecodedSymmetricKeyLength) != 0) abort();
    if (mbedtls_md_hmac_update(&ctx, &signature[0], signature.size()) != 0) abort();
    if (mbedtls_md_hmac_finish(&ctx, encryptedSignature) != 0) abort();

    // Base64 encode encrypted signature
    // <-- encryptedSignature
    // --> b64encHmacsha256Signature
    char b64encHmacsha256Signature[(size_t)(sizeof(encryptedSignature) * 1.5f) + 1];
    size_t b64encHmacsha256SignatureLength;
    if (mbedtls_base64_encode((unsigned char*)b64encHmacsha256Signature, sizeof(b64encHmacsha256Signature), &b64encHmacsha256SignatureLength, encryptedSignature, mbedtls_md_get_size(mbedtls_md_info_from_type(mdType))) != 0) abort();

    return std::string(b64encHmacsha256Signature, b64encHmacsha256SignatureLength);
}
