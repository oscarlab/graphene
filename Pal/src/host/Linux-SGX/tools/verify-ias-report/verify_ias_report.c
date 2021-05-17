/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2018-2020 Invisible Things Lab
 *                         Rafal Wojdyla <omeg@invisiblethingslab.com>
 */

#define _GNU_SOURCE

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "attestation.h"
#include "util.h"

struct option g_options[] = {
    { "help", no_argument, 0, 'h' },
    { "verbose", no_argument, 0, 'v' },
    { "msb", no_argument, 0, 'm' },
    { "report-path", required_argument, 0, 'r' },
    { "sig-path", required_argument, 0, 's' },
    { "allow-outdated-tcb", no_argument, 0, 'o' },
    { "nonce", required_argument, 0, 'n' },
    { "mr-signer", required_argument, 0, 'S' },
    { "mr-enclave", required_argument, 0, 'E' },
    { "report-data", required_argument, 0, 'R' },
    { "isv-prod-id", required_argument, 0, 'P' },
    { "isv-svn", required_argument, 0, 'S' },
    { "ias-pubkey", required_argument, 0, 'i' },
    { 0, 0, 0, 0 }
};

static void usage(const char* exec) {
    INFO("Usage: %s [options]\n", exec);
    INFO("Available options:\n");
    INFO("  --help, -h                Display this help\n");
    INFO("  --verbose, -v             Enable verbose output\n");
    INFO("  --msb, -m                 Print/parse hex strings in big-endian order\n");
    INFO("  --report-path, -r PATH    Path to the IAS report\n");
    INFO("  --sig-path, -s PATH       Path to the IAS report's signature\n");
    INFO("  --allow-outdated-tcb, -o  Treat IAS status GROUP_OUT_OF_DATE as OK\n");
    INFO("  --nonce, -n STRING        Nonce that's expected in the report (optional)\n");
    INFO("  --mr-signer, -S STRING    Expected mr_signer field (hex string, optional)\n");
    INFO("  --mr-enclave, -E STRING   Expected mr_enclave field (hex string, optional)\n");
    INFO("  --report-data, -R STRING  Expected report_data field (hex string, optional)\n");
    INFO("  --isv-prod-id, -P NUMBER  Expected isv_prod_id field (uint16_t, optional)\n");
    INFO("  --isv-svn, -V NUMBER      Expected isv_svn field (uint16_t, optional)\n");
    INFO("  --ias-pubkey, -i PATH     Path to IAS public RSA key (PEM format, optional)\n");
}

int main(int argc, char* argv[]) {
    int option              = 0;
    char* report_path       = NULL;
    size_t report_size      = 0;
    char* sig_path          = NULL;
    size_t sig_size         = 0;
    char* nonce             = NULL;
    bool allow_outdated_tcb = false;
    char* mrsigner          = NULL;
    char* mrenclave         = NULL;
    char* report_data       = NULL;
    char* isv_prod_id       = NULL;
    char* isv_svn           = NULL;
    char* ias_pubkey_path   = NULL;
    endianness_t endian     = ENDIAN_LSB;

    // parse command line
    while (true) {
        option = getopt_long(argc, argv, "hvmr:s:on:S:E:R:P:V:i:", g_options, NULL);
        if (option == -1)
            break;

        switch (option) {
            case 'h':
                usage(argv[0]);
                return 0;
            case 'v':
                set_verbose(true);
                break;
            case 'm':
                endian = ENDIAN_MSB;
                break;
            case 'r':
                report_path = optarg;
                break;
            case 's':
                sig_path = optarg;
                break;
            case 'o':
                allow_outdated_tcb = true;
                break;
            case 'n':
                nonce = optarg;
                break;
            case 'S':
                mrsigner = optarg;
                break;
            case 'E':
                mrenclave = optarg;
                break;
            case 'R':
                report_data = optarg;
                break;
            case 'P':
                isv_prod_id = optarg;
                break;
            case 'V':
                isv_svn = optarg;
                break;
            case 'i':
                ias_pubkey_path = optarg;
                break;
            default:
                usage(argv[0]);
                return -1;
        }
    }

    set_endianness(endian);

    if (!report_path || !sig_path) {
        usage(argv[0]);
        return -1;
    }

    void* report = read_file(report_path, &report_size, /*buffer=*/NULL);
    if (!report) {
        ERROR("Failed to read report file '%s'\n", report_path);
        return -1;
    }

    void* sig = read_file(sig_path, &sig_size, /*buffer=*/NULL);
    if (!sig) {
        ERROR("Failed to read report signature file '%s'\n", sig_path);
        return -1;
    }

    char* ias_pubkey = NULL;
    size_t ias_pubkey_size = 0;

    if (ias_pubkey_path) {
        void* buf = read_file(ias_pubkey_path, &ias_pubkey_size, /*buffer=*/NULL);
        if (!buf) {
            ERROR("Failed to read IAS pubkey file '%s'\n", ias_pubkey_path);
            return -1;
        }

        // Need to add NULL terminator
        ias_pubkey = calloc(1, ias_pubkey_size + 1);
        if (!ias_pubkey) {
            ERROR("No memory\n");
            return -1;
        }

        memcpy(ias_pubkey, buf, ias_pubkey_size);
        free(buf);
        DBG("Using IAS public key from file '%s'\n", ias_pubkey_path);
    }

    int ret = verify_ias_report(report, report_size, sig, sig_size, allow_outdated_tcb, nonce,
                                mrsigner, mrenclave, isv_prod_id, isv_svn, report_data, ias_pubkey,
                                /*expected_as_str=*/true);

    return ret;
}
