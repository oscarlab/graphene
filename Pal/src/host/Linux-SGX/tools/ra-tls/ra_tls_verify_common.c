/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2018-2020 Intel Labs */

/*!
 * \file
 *
 * This file contains the common code of verification callbacks for TLS libraries. All functions
 * here have hidden visibility (not accessible from outside the shared library).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include "attestation.h"
#include "ra_tls.h"
#include "util.h"

verify_measurements_cb_t g_verify_measurements_cb = NULL;

/* annoyingly, mbedTLS doesn't provide Base64URL encoding, so need to add this helper; see
 * https://github.com/ARMmbed/mbedtls/issues/1285; the below code is adapted from there */
static const uint8_t base64url_enc_map[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
    'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4',
    '5', '6', '7', '8', '9', '-', '_' };

static int base64url_encode(uint8_t* dst, size_t dst_size, size_t* actual_size, const uint8_t* src,
                            size_t src_size) {
    if (!src_size) {
        *actual_size = 0;
        return 0;
    }

    size_t n = src_size / 3;
    if (src_size % 3)
        n += 1;

    n *= 4;

    if (!dst || dst_size < n + 1) {
        *actual_size = n + 1;
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    }

    n = (src_size / 3) * 3;

    size_t i;
    int C1, C2, C3;
    uint8_t* p = dst;

    for (i = 0; i < n; i += 3) {
        C1 = *src++;
        C2 = *src++;
        C3 = *src++;

        *p++ = base64url_enc_map[(C1 >> 2) & 0x3F];
        *p++ = base64url_enc_map[(((C1 &  3) << 4) + (C2 >> 4)) & 0x3F];
        *p++ = base64url_enc_map[(((C2 & 15) << 2) + (C3 >> 6)) & 0x3F];
        *p++ = base64url_enc_map[C3 & 0x3F];
    }

    if (i < src_size) {
        C1 = *src++;
        C2 = (i + 1 < src_size) ? *src++ : 0;

        *p++ = base64url_enc_map[(C1 >> 2) & 0x3F];
        *p++ = base64url_enc_map[(((C1 & 3) << 4) + (C2 >> 4)) & 0x3F];

        if (i + 1 < src_size)
            *p++ = base64url_enc_map[((C2 & 15) << 2) & 0x3F];
    }

    *actual_size = p - dst;
    *p = 0;
    return 0;
}

static int getenv_enclave_measurements(sgx_measurement_t* mrsigner, bool* validate_mrsigner,
                                       sgx_measurement_t* mrenclave, bool* validate_mrenclave,
                                       sgx_prod_id_t* isv_prod_id, bool* validate_isv_prod_id,
                                       sgx_isv_svn_t* isv_svn, bool* validate_isv_svn) {
    *validate_mrsigner    = false;
    *validate_mrenclave   = false;
    *validate_isv_prod_id = false;
    *validate_isv_svn     = false;

    const char* mrsigner_hex;
    const char* mrenclave_hex;
    const char* isv_prod_id_dec;
    const char* isv_svn_dec;

    /* any of the below variables may be NULL (and then not used in validation) */
    mrsigner_hex = getenv(RA_TLS_MRSIGNER);
    if (mrsigner_hex) {
        if (parse_hex(mrsigner_hex, mrsigner, sizeof(*mrsigner)) != 0)
            return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
        *validate_mrsigner = true;
    }

    mrenclave_hex = getenv(RA_TLS_MRENCLAVE);
    if (mrenclave_hex) {
        if (parse_hex(mrenclave_hex, mrenclave, sizeof(*mrenclave)) != 0)
            return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
        *validate_mrenclave = true;
    }

    isv_prod_id_dec = getenv(RA_TLS_ISV_PROD_ID);
    if (isv_prod_id_dec) {
        errno = 0;
        *isv_prod_id = strtoul(isv_prod_id_dec, NULL, 10);
        if (errno)
            return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
        *validate_isv_prod_id = true;
    }

    isv_svn_dec = getenv(RA_TLS_ISV_SVN);
    if (isv_svn_dec) {
        errno = 0;
        *isv_svn = strtoul(isv_svn_dec, NULL, 10);
        if (errno)
            return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
        *validate_isv_svn = true;
    }

    return 0;
}

int getenv_allow_outdated_tcb(bool* allow_outdated_tcb) {
    *allow_outdated_tcb = false;

    char* str = getenv(RA_TLS_ALLOW_OUTDATED_TCB_INSECURE);
    if (!str)
        return 0;

    if (!strcmp(str, "1") || !strcmp(str, "true") || !strcmp(str, "TRUE"))
        *allow_outdated_tcb = true;

    return 0;
}

/*! searches for specific \p oid among \p exts and returns pointer to its value in \p val */
int find_oid(const uint8_t* exts, size_t exts_len, const uint8_t* oid, size_t oid_len,
             uint8_t** val, size_t* len) {
    /* TODO: searching with memmem is not robust (what if some extension contains exactly these
     *       chars?), but mbedTLS has nothing generic enough for our purposes; this is still
     *       secure because this func is used for extracting the SGX quote which is verified
     *       later, but may lead to unexpected failures (hardly possible in real world though) */
    uint8_t* p = memmem(exts, exts_len, oid, oid_len);
    if (!p)
        return MBEDTLS_ERR_X509_INVALID_EXTENSIONS;

    const uint8_t* exts_end = exts + exts_len;

    /* move pointer past OID string and to the OID value */
    p += oid_len;

    if (p >= exts_end)
        return MBEDTLS_ERR_X509_INVALID_EXTENSIONS;

    if (*p == 0x01) {
        /* some TLS libs generate a BOOLEAN for the criticality of the extension before the
         * extension value itself; check its value and skip it */
        p++;
        if (p >= exts_end || *p++ != 0x01) {
            /* BOOLEAN length must be 0x01 */
            return MBEDTLS_ERR_X509_INVALID_EXTENSIONS;
        }
        if (p >= exts_end || *p++ != 0x00) {
            /* BOOLEAN value must be 0x00 (non-critical extension) */
            return MBEDTLS_ERR_X509_INVALID_EXTENSIONS;
        }
    }

    /* now comes the octet string */
    if (p >= exts_end || *p++ != 0x04) {
        /* tag for octet string must be 0x04 */
        return MBEDTLS_ERR_X509_INVALID_EXTENSIONS;
    }
    if (p >= exts_end || *p++ != 0x82) {
        /* length of octet string must be 0x82 (encoded in two bytes) */
        return MBEDTLS_ERR_X509_INVALID_EXTENSIONS;
    }

    if (p + 2 >= exts_end)
        return MBEDTLS_ERR_X509_INVALID_EXTENSIONS;

    *len   = *p++;
    *len <<= 8;
    *len  += *p++;

    *val = p;

    if (*len > QUOTE_MAX_SIZE || *val + *len > exts_end)
        return MBEDTLS_ERR_X509_INVALID_EXTENSIONS;

    return 0;
}

/*! calculate sha256 over public key from \p crt and copy it into \p sha */
static int sha256_over_crt_pk(mbedtls_x509_crt* crt, uint8_t* sha) {
    uint8_t pk_der[PUB_KEY_SIZE_MAX] = {0};

    /* below function writes data at the end of the buffer */
    int pk_der_size_byte = mbedtls_pk_write_pubkey_der(&crt->pk, pk_der, PUB_KEY_SIZE_MAX);
    if (pk_der_size_byte != RSA_PUB_3072_KEY_DER_LEN)
        return MBEDTLS_ERR_PK_INVALID_PUBKEY;

    /* move the data to the beginning of the buffer, to avoid pointer arithmetic later */
    memmove(pk_der, pk_der + PUB_KEY_SIZE_MAX - pk_der_size_byte, pk_der_size_byte);

    return mbedtls_sha256_ret(pk_der, pk_der_size_byte, sha, /*is224=*/0);
}

/*! compares if report_data from \quote corresponds to sha256 of public key in \p crt */
int cmp_crt_pk_against_quote_report_data(mbedtls_x509_crt* crt, sgx_quote_t* quote) {
    int ret;

    uint8_t sha[SHA256_DIGEST_SIZE];
    ret = sha256_over_crt_pk(crt, sha);
    if (ret < 0)
        return ret;

    ret = memcmp(quote->report_body.report_data.d, sha, SHA256_DIGEST_SIZE);
    if (ret)
        return MBEDTLS_ERR_X509_SIG_MISMATCH;

    return 0;
}

void ra_tls_set_measurement_callback(int (*f_cb)(const char* mrenclave, const char* mrsigner,
                                                 const char* isv_prod_id, const char* isv_svn)) {
    g_verify_measurements_cb = f_cb;
}

int ra_tls_verify_callback_der(uint8_t* der_crt, size_t der_crt_size) {
    int ret;
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);

    ret = mbedtls_x509_crt_parse(&crt, der_crt, der_crt_size);
    if (ret < 0)
        goto out;

    ret = ra_tls_verify_callback(/*unused data=*/NULL, &crt, /*depth=*/0, /*flags=*/NULL);
    if (ret < 0)
        goto out;

    ret = 0;
out:
    mbedtls_x509_crt_free(&crt);
    return ret;
}

int verify_quote_against_envvar_measurements(const void* quote, size_t quote_size) {
    int ret;

    sgx_measurement_t expected_mrsigner;
    sgx_measurement_t expected_mrenclave;
    sgx_prod_id_t expected_isv_prod_id;
    sgx_isv_svn_t expected_isv_svn;

    bool validate_mrsigner    = false;
    bool validate_mrenclave   = false;
    bool validate_isv_prod_id = false;
    bool validate_isv_svn     = false;

    ret = getenv_enclave_measurements(&expected_mrsigner, &validate_mrsigner,
                                      &expected_mrenclave, &validate_mrenclave,
                                      &expected_isv_prod_id, &validate_isv_prod_id,
                                      &expected_isv_svn, &validate_isv_svn);
    if (ret < 0)
        return MBEDTLS_ERR_X509_BAD_INPUT_DATA;

    ret = verify_quote(quote, quote_size,
                       validate_mrsigner ? (char*)&expected_mrsigner : NULL,
                       validate_mrenclave ? (char*)&expected_mrenclave : NULL,
                       validate_isv_prod_id ? (char*)&expected_isv_prod_id : NULL,
                       validate_isv_svn ? (char*)&expected_isv_svn : NULL,
                       /*report_data=*/NULL, /*expected_as_str=*/false);
    if (ret < 0)
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;

    return 0;
}

/* generate Microsoft Azure Attestation (MAA) compatible JSON file with fields like "QuoteHex",
 * "EnclaveHeldDataHex", etc. to be sent MAA to for SGX remote attestation */
int generate_maa_json_file(const void* quote, size_t quote_size, mbedtls_x509_crt* crt) {
    int ret;

    char* json_file = getenv(RA_TLS_MAA_JSON_FILE);
    if (!json_file) {
        /* no filename provided, user doesn't want to create the MAA JSON file */
        return 0;
    }

    /* make a copy of filename string immediately since getenv is not reentrant */
    json_file = strdup(json_file);
    if (!json_file)
        return MBEDTLS_ERR_X509_ALLOC_FAILED;

    /* allocate enough memory for SGX quote in base64Url format */
    char* quote_base64url = calloc(1, QUOTE_MAX_SIZE * 2);
    if (!quote_base64url) {
        free(json_file);
        return MBEDTLS_ERR_X509_ALLOC_FAILED;
    }

    /* allocate enough memory for JSON content (mostly SGX quote in Base64URL format) */
    char* json_content = calloc(1, MAA_JSON_FILE_MAX_SIZE);
    if (!json_content) {
        free(json_file);
        free(quote_base64url);
        return MBEDTLS_ERR_X509_ALLOC_FAILED;
    }

    int bytes_printed;
    char* json_content_ptr = json_content;

    /* "Type" is always 2 for Intel SGX quotes */
    bytes_printed = sprintf(json_content_ptr, "{ \"Type\": 2,\n");
    if (bytes_printed < 0) {
        ret = MBEDTLS_ERR_X509_ALLOC_FAILED;
        goto out;
    }
    json_content_ptr += bytes_printed;
    assert(json_content_ptr - json_content < MAA_JSON_FILE_MAX_SIZE);

    /* "EnclaveHeldDataHex" is a Base64URL-encoded public key in DER format of the RA-TLS cert */
    uint8_t pk_der[PUB_KEY_SIZE_MAX] = {0};
    int pk_der_size = mbedtls_pk_write_pubkey_der(&crt->pk, pk_der, PUB_KEY_SIZE_MAX);
    if (pk_der_size != RSA_PUB_3072_KEY_DER_LEN) {
        ret = MBEDTLS_ERR_PK_INVALID_PUBKEY;
        goto out;
    }

    memmove(pk_der, pk_der + PUB_KEY_SIZE_MAX - pk_der_size, pk_der_size);

    uint8_t pk_der_base64url[PUB_KEY_SIZE_MAX * 2] = {0};
    size_t pk_der_base64url_size = 0;
    ret = base64url_encode(pk_der_base64url, sizeof(pk_der_base64url), &pk_der_base64url_size,
                           pk_der, pk_der_size);
    if (ret < 0)
        goto out;

    assert(pk_der_base64url_size);
    bytes_printed = sprintf(json_content_ptr, "\"EnclaveHeldDataHex\": \"%s\",\n",
                            pk_der_base64url);
    if (bytes_printed < 0) {
        ret = MBEDTLS_ERR_X509_ALLOC_FAILED;
        goto out;
    }
    json_content_ptr += bytes_printed;
    assert(json_content_ptr - json_content < MAA_JSON_FILE_MAX_SIZE);

    /* "QuoteHex" is a Base64URL-encoded SGX quote */
    size_t quote_base64url_size = 0;
    ret = base64url_encode((uint8_t*)quote_base64url, QUOTE_MAX_SIZE * 2, &quote_base64url_size,
                           quote, quote_size);
    if (ret < 0)
        goto out;

    assert(quote_base64url_size);
    bytes_printed = sprintf(json_content_ptr, "\"QuoteHex\": \"%s\"\n", quote_base64url);
    if (bytes_printed < 0) {
        ret = MBEDTLS_ERR_X509_ALLOC_FAILED;
        goto out;
    }
    json_content_ptr += bytes_printed;
    assert(json_content_ptr - json_content < MAA_JSON_FILE_MAX_SIZE);

    /* finalize JSON file content and write into a file */
    bytes_printed = sprintf(json_content_ptr, "}");
    if (bytes_printed < 0) {
        ret = MBEDTLS_ERR_X509_ALLOC_FAILED;
        goto out;
    }
    json_content_ptr += bytes_printed;
    assert(json_content_ptr - json_content < MAA_JSON_FILE_MAX_SIZE);

    int fd = open(json_file, O_CREAT | O_WRONLY,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd < 0) {
        ret = -errno;
        goto out;
    }

    ssize_t len = json_content_ptr - json_content;
    ssize_t bytes = 0;
    while ((ssize_t)len > bytes) {
        ssize_t x = write(fd, json_content + bytes, len - bytes);
        if (x < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (x < 0) {
            ret = -errno;
            goto out;
        }
        bytes += x;
    }

    ret = close(fd);
    if (ret < 0) {
        ret = -errno;
        goto out;
    }

    ret = 0;
out:
    free(json_file);
    free(json_content);
    free(quote_base64url);
    return ret;
}
