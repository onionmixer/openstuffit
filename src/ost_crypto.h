#ifndef OST_CRYPTO_H
#define OST_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ost_md5_5(const uint8_t *data, size_t size, uint8_t out[5]);
void ost_rc4_crypt(const uint8_t *key, size_t key_size, const uint8_t *src, size_t size, uint8_t *dst);
bool ost_classic_des_key(const uint8_t *password,
                         size_t password_size,
                         const uint8_t mkey[8],
                         const uint8_t entry_key[16],
                         uint8_t out_key[16]);
void ost_classic_des_decrypt_payload(const uint8_t key[16], const uint8_t *src, size_t size, uint8_t *dst);

#endif
