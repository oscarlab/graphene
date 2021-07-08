SGX tools
=========

.. TODO move this to Documentation/manpages

SGX availability checker
------------------------

Example output::

    > ./is_sgx_available/is_sgx_available
    SGX supported by CPU: true
    SGX1 (ECREATE, EENTER, ...): true
    SGX2 (EAUG, EACCEPT, EMODPR, ...): false
    Flexible Launch Control (IA32_SGXPUBKEYHASH{0..3} MSRs): false
    SGX extensions for virtualizers (EINCVIRTCHILD, EDECVIRTCHILD, ESETCONTEXT): false
    Extensions for concurrent memory management (ETRACKC, ELDBC, ELDUC, ERDINFO): false
    Max enclave size (32-bit): 0x80000000
    Max enclave size (64-bit): 0x1000000000
    EPC size: 0x5d80000
    SGX driver loaded: true
    SGX PSW/libsgx installed: true
    AESMD running: false

The program terminates successfully if all SGX1 components are detected and running, otherwise
the program exits with an error code (see the source code for possible values).
To suppress printing output use the --quiet argument.


SGX quote dump
--------------

Displays internal structure of an SGX quote::

    Usage: quote_dump [options] <quote path>
    Available options:
      --help, -h  Display this help
      --msb, -m   Display hex strings in big-endian order

    $ quote_dump -m gr.quote
    version           : 0002
    sign_type         : 0001
    epid_group_id     : 00000aef
    qe_svn            : 0007
    pce_svn           : 0006
    xeid              : 00000000
    basename          : 0000000000000000000000000000000094b929a21f249e5eccb9a5fa33fa5a65
    report_body       :
     cpu_svn          : 000000000000000000000201ffff0e08
     misc_select      : 00000000
     reserved1        : 000000000000000000000000
     isv_ext_prod_id  : 00000000000000000000000000000000
     attributes.flags : 0000000000000007
     attributes.xfrm  : 000000000000001f
     mr_enclave       : 4d69102c40401f40a54eb156601be73fb7605db0601845580f036fd284b7b303
     reserved2        : 0000000000000000000000000000000000000000000000000000000000000000
     mr_signer        : 14b284525c45c4f526bf1535d05bd88aa73b9e184464f2d97be3dabc0d187b57
     reserved3        : 0000000000000000000000000000000000000000000000000000000000000000
     config_id        : 00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
     isv_prod_id      : 0000
     isv_svn          : 0000
     config_svn       : 0000
     reserved4        : 000000000000000000000000000000000000000000000000000000000000000000000000000000000000
     isv_family_id    : 00000000000000000000000000000000
     report_data      : 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004ba476e321e12c720000000000000001
    signature_len     : 680 (0x2a8)
    signature         : 2e13c6a3...


Intel Attestation Service submitter
-----------------------------------

Submits requests to Intel Attestation Service (IAS) for retrieving EPID signature revocation lists
and for verifying attestation evidence (enclave quote)::

    Usage: ias_request <request> [options]
    Available requests:
      sigrl                     Retrieve signature revocation list for a given EPID group
      report                    Verify attestation evidence (quote)
    Available general options:
      --help, -h                Display this help
      --verbose, -v             Enable verbose output
      --msb, -m                 Print/parse hex strings in big-endian order
      --api-key, -k STRING      IAS API key
    Available sigrl options:
      --gid, -g STRING          EPID group ID (hex string)
      --sigrl-path, -i PATH     Path to save SigRL to
      --sigrl-url, -S URL       URL for the IAS SigRL endpoint (default:
                                https://api.trustedservices.intel.com/sgx/dev/attestation/v3/sigrl)
    Available report options:
      --quote-path, -q PATH     Path to quote to submit
      --report-path, -r PATH    Path to save IAS report to
      --sig-path, -s PATH       Path to save IAS report's signature to
      --nonce, -n STRING        Nonce to use (optional)
      --cert-path, -c PATH      Path to save IAS certificate to (optional)
      --advisory-path, -a PATH  Path to save IAS security advisories to (optional)
      --report-url, -R URL      URL for the IAS attestation report endpoint (default:
                                https://api.trustedservices.intel.com/sgx/dev/attestation/v3/report)

Example SigRL retrieval::

    $ ias_request sigrl -k $IAS_API_KEY -g ef0a0000 -i sigrl
    No SigRL for given EPID group ID ef0a0000

Example quote verification::

    $ ias_request report -k $IAS_API_KEY -q gr.quote -r ias.report -s ias.sig -c ias.cert -a ias.adv -v
    Verbose output enabled
    IAS request:
    {"isvEnclaveQuote":"AgABAO8..."}
    [...snip curl output...]
    IAS response: 200
    IAS report saved to: ias.report
    IAS report signature saved to: ias.sig
    IAS certificate saved to: ias.cert
    IAS advisory saved to: ias.adv
    IAS submission successful
    $ cat ias.report
    {"id":"205146415611480061439763344693868541328","timestamp":"2020-03-20T10:48:32.353294","version":3,"epidPseudonym":"Itmg0 [...]","isvEnclaveQuoteStatus":"GROUP_OUT_OF_DATE" [...]}


Intel Attestation Report verifier
---------------------------------

Verifies attestation report retrieved from IAS (using ``ias_request`` for example). Also verifies
that the quote from the report contains expected values::

    Usage: verify_ias_report [options]
    Available options:
      --help, -h                Display this help
      --verbose, -v             Enable verbose output
      --msb, -m                 Print/parse hex strings in big-endian order
      --report-path, -r PATH    Path to the IAS report
      --sig-path, -s PATH       Path to the IAS report's signature
      --allow-outdated-tcb, -o  Treat IAS status GROUP_OUT_OF_DATE as OK
      --nonce, -n STRING        Nonce that's expected in the report (optional)
      --mr-signer, -S STRING    Expected mr_signer field (hex string, optional)
      --mr-enclave, -E STRING   Expected mr_enclave field (hex string, optional)
      --report-data, -R STRING  Expected report_data field (hex string, optional)
      --isv-prod-id, -P NUMBER  Expected isv_prod_id field (uint16_t, optional)
      --isv-svn, -V NUMBER      Expected isv_svn field (uint16_t, optional)
      --ias-pubkey, -i PATH     Path to IAS public RSA key (PEM format, optional)

Example report verification with all options enabled::

    $ verify_ias_report -v -m -r rp -s sp -i ias.pem -o -n thisisnonce -S 14b284525c45c4f526bf1535d05bd88aa73b9e184464f2d97be3dabc0d187b57 -E 4d69102c40401f40a54eb156601be73fb7605db0601845580f036fd284b7b303 -R 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004ba476e321e12c720000000000000001 -P 0 -V 0
    Verbose output enabled
    Endianness set to MSB
    Using IAS public key from file 'ias.pem'
    IAS key: RSA, 2048 bits
    Decoded IAS signature size: 256 bytes
    IAS report: signature verified correctly
    IAS report: allowing quote status GROUP_OUT_OF_DATE
    IAS report: nonce OK
    IAS report: quote decoded, size 432 bytes
    [...quote dump...]
    Quote: mr_signer OK
    Quote: mr_enclave OK
    Quote: isv_prod_id OK
    Quote: isv_svn OK
    Quote: report_data OK


RA-TLS Libraries
----------------

RA-TLS integrates Intel SGX remote attestation into the TLS connection setup.
Conceptually, it extends the standard X.509 certificate with SGX-related
information. The additional information allows the receiver (verifier) of the
certificate to verify that it is indeed communicating with an SGX enclave
(attester). RA-TLS is shipped as three libraries: ``ra_tls_attest.so``,
EPID-based ``ra_tls_verify_epid.so`` and ECDSA-based (DCAP)
``ra_tls_verify_dcap.so``.

For more information on RA-TLS, please read the ``Attestation`` documentation of
Graphene.


Secret Provisioning Libraries
-----------------------------

Secret Provisioning libraries are reference implementations for the flows to
provision secrets from a trusted machine (service, verifier) to an enclavized
application (client, attester). These libraries rely heavily on RA-TLS.

Conceptually, an enclavized client application and a trusted service establish a
secure RA-TLS communication channel via TLS mutual attestation. The service
sends its normal X.509 certificate for verification by client, whereas the
enclavized client sends its RA-TLS X.509 certificate with SGX-related
information for verification by the service. After this mutual attestation, the
trust is established, and the service provisions the secrets to the enclavized
client. The established TLS channel may be either closed after provisioning
these initial secrets or may be further used by both parties for continued
secure communication.

Secret Provisioning is shipped as three libraries: ``secret_prov_attest.so``,
EPID-based ``secret_prov_verify_epid.so`` and ECDSA-based (DCAP)
``secret_prov_verify_dcap.so``.

For more information on Secret Provisioning, please read the ``Attestation``
documentation of Graphene.
