#include "Arduino.h"
#include "az_iot_helpers.h"

#include "az_span.h"

#include "NTP.h"
#include <time.h>

#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))

#define ONE_HOUR_IN_SECS 3600

static uint8_t signature[1024];
static unsigned char encrypted_signature[32];
static unsigned char base64_decoded_device_key[32];

int generateSasToken(const az_iot_hub_client *iot_hub_client, const char *device_key, uint32_t expiration, char *sas_token, size_t size)
{
  az_span signature_span = az_span_create((uint8_t *)signature, sizeofarray(signature));
  az_span out_signature_span;

  // Get signature
  if (az_failed(az_iot_hub_client_sas_get_signature(
          iot_hub_client, expiration, signature_span, &out_signature_span)))
  {
    return 1;
  }

  // Base64-decode device key
  size_t base64_decoded_device_key_length;
  mbedtls_base64_decode(base64_decoded_device_key, 32, &base64_decoded_device_key_length, (unsigned char *)device_key, strlen(device_key));

  if (base64_decoded_device_key_length == 0)
  {
    return 1;
  }

  // SHA-256 encrypt
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1); //use hmac
  mbedtls_md_hmac_starts(&ctx, base64_decoded_device_key, base64_decoded_device_key_length);
  mbedtls_md_hmac_update(&ctx, az_span_ptr(out_signature_span), az_span_size(out_signature_span));
  mbedtls_md_hmac_finish(&ctx, encrypted_signature);

  // Base64 encode encrypted signature
  char b64enc_hmacsha256_signature[64];
  size_t encrypted_sig_length;
  mbedtls_base64_encode((unsigned char *)b64enc_hmacsha256_signature, 64, &encrypted_sig_length, encrypted_signature, mbedtls_md_get_size(mbedtls_md_info_from_type(md_type)));

  az_span b64enc_hmacsha256_signature_span = az_span_create(
      (uint8_t *)b64enc_hmacsha256_signature, encrypted_sig_length);

  // URl-encode base64 encoded encrypted signature
  if (az_failed(az_iot_hub_client_sas_get_password(
          iot_hub_client,
          b64enc_hmacsha256_signature_span,
          expiration,
          AZ_SPAN_NULL,
          sas_token,
          size,
          NULL)))
  {
    return 1;
  }

  return 0;
}
