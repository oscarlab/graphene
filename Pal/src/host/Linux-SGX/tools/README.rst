SGX tools
=========


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

RA-TLS integrates Intel SGX remote attestation into the TLS connection setup. Conceptually, it
extends the standard X.509 certificate with SGX-related information. The additional information
allows the receiver (verifier) of the certificate to verify that it is indeed communicating with
an SGX enclave (attester). RA-TLS is shipped as three libraries: ``ra_tls_attest.so``, EPID-based
``ra_tls_verify_epid.so`` and ECDSA-based (DCAP) ``ra_tls_verify_dcap.so``.

``ra_tls_attest.so``
^^^^^^^^^^^^^^^^^^^^

This library creates the self-signed RA-TLS certificate. It must be loaded into the SGX enclave.
This library relies on the pseudo-FS ``/dev/attestation`` to retrieve the SGX quote and embed it
into the RA-TLS certificate. Typically linked into server applications. Not thread-safe.

The library expects the following information in the manifest for EPID-based attestation:

- ``sgx.remote_attestation = 1`` -- remote attestation is enabled.
- ``sgx.ra_client_spid`` -- client SPID for EPID remote attestation.
- ``sgx.ra_client_linkable`` -- client linkable/unlinkable attestation policy.

For ECDSA-based (DCAP) attestation, the library expects instead:

- ``sgx.remote_attestation = 1`` -- remote attestation is enabled.
- ``sgx.ra_client_spid = <empty string>`` -- it is DCAP attestation, *not* EPID attestation.

The library uses the following environment variables if available:

- ``RA_TLS_CERT_TIMESTAMP_NOT_BEFORE`` -- the generated RA-TLS certificate uses this
  timestamp-not-before value, in the format "20010101000000" (this is also the default value if
  environment variable is not available).
- ``RA_TLS_CERT_TIMESTAMP_NOT_AFTER`` -- the generated RA-TLS certificate uses this
  timestamp-not-after value, in the format "20301231235959" (this is also the default value if
  environment variable is not available).

``ra_tls_verify_epid.so``
^^^^^^^^^^^^^^^^^^^^^^^^^

This library contains the verification callback that should be registered with the TLS library
during verification of the TLS certificate. It verifies the RA-TLS certificate and the SGX quote by
sending it to the Intel Attestation Service (IAS) and retrieving the attestation report from IAS.
Typically linked into client applications. Not thread-safe.

The library uses the following SGX-specific environment variables, representing SGX measurements,
if available:

- ``RA_TLS_MRSIGNER`` (optional) -- verify that the server enclave has this ``MRSIGNER``. This is a
  hex string.
- ``RA_TLS_MRENCLAVE`` (optional) -- verify that the server enclave has this ``MRENCLAVE``. This is
  a hex string.
- ``RA_TLS_ISV_PROD_ID`` (optional) -- verify that the server enclave has this ``ISV_PROD_ID``.
  This is a decimal string.
- ``RA_TLS_ISV_SVN`` (optional) -- verify that the server enclave has this ``ISV_SVN``. This is a
  decimal string.

The four SGX measurements above may be also verified via a user-specified callback with the
signature ``int (*callback)(char* mrenclave, char* mrsigner, char* isv_prod_id, char* isv_svn)``.
This callback must be registered via ``ra_tls_set_measurement_callback()``. The measurements from
the received SGX quote are passed as four arguments. It is up to the user to implement the correct
verification of SGX measurements in this callback (e.g., by comparing against expected values stored
in a central database).

The library also uses the following SGX-specific environment variable:

- ``RA_TLS_ALLOW_OUTDATED_TCB_INSECURE`` (optional) -- whether to allow outdated TCB as returned in
  the IAS attestation report or returned by the DCAP verification library. Values ``1/true/TRUE``
  mean "allow outdated TCB". Note that allowing outdated TCB is **insecure** and should be used
  only for debugging and testing. Outdated TCB is not allowed by default.

The library uses the following EPID-specific environment variables if available:

- ``RA_TLS_EPID_API_KEY`` (mandatory) -- client API key for EPID remote attestation.
- ``RA_TLS_IAS_REPORT_URL`` (optional) -- URL for IAS "verify attestation evidence" API endpoint.
  If not specified, the default hard-coded URL for IAS is used.
- ``RA_TLS_IAS_SIGRL_URL`` (optional) -- URL for IAS "Retrieve SigRL" API endpoint. If not
  specified, the default hard-coded URL for IAS is used.
- ``RA_TLS_IAS_PUB_KEY_PEM`` (optional) -- public key of IAS. If not specified, the default
  hard-coded public key is used.

The library uses the following Microsoft Azure Attestation (MAA) specific environment variables if
available:

- ``RA_TLS_MAA_JSON_FILE`` (optional) -- filename where the MAA-specific JSON content is generated.
  The generated file contains "Type = 2", "EnclaveHeldDataHex" that contains the base64Url-encoded
  public key of the RA-TLS certificate, and "QuoteHex" that contains the base64Url-encoded SGX
  quote.

``ra_tls_verify_dcap.so``
^^^^^^^^^^^^^^^^^^^^^^^^^

Similarly to ``ra_tls_verify_epid.so``, this library contains the verification callback that
should be registered with the TLS library during verification of the TLS certificate. Verifies
the RA-TLS certificate and the SGX quote by forwarding it to DCAP verification library
(``libsgx_dcap_quoteverify.so``) and checking the result. Typically linked into client
applications. Not thread-safe.

The library uses the same SGX-specific environment variables as ``ra_tls_verify_epid.so`` and
ignores the EPID-specific environment variables. Similarly to the EPID version, instead of using
environment variables, the four SGX measurements may be verified via a user-specified callback
registered via ``ra_tls_set_measurement_callback()``.

The library expects all the DCAP infrastructure to be installed and working correctly on the host.


Secret Provisioning Libraries
-----------------------------

Secret Provisioning libraries are reference implementations for the flows to provision secrets from
a trusted machine (server, verifier) to an enclavized application (client, attester). These
libraries rely heavily on RA-TLS and re-use the same configuration parameters as listed above.

Conceptually, a client application and a trusted server establish a secure RA-TLS communication
channel via TLS mutual attestation. The server sends its normal X.509 certificate for verification
by client, whereas the client sends its RA-TLS X.509 certificate with SGX-related information for
verification by server. After this mutual attestation, the trust is established, and the server
provisions the secrets to the client. The established TLS channel may be either closed after
provisioning these initial secrets or may be further used by both parties for continued secure
communication.

Secret Provisioning is shipped as three libraries: ``secret_prov_attest.so``, EPID-based
``secret_prov_verify_epid.so`` and ECDSA-based (DCAP) ``secret_prov_verify_dcap.so``.

``secret_prov_attest.so``
^^^^^^^^^^^^^^^^^^^^^^^^^

This library is typically linked into client (enclavized) applications. The application calls into
this library to initiate the RA-TLS session with the remote trusted server for secret provisioning.
Alternatively, the library runs before application's entry point, initializes the RA-TLS session,
receives the secret and stashes it in an environment variable ``SECRET_PROVISION_SECRET_STRING``.
In both cases, the application may call into the library to continue secure communication with the
trusted server and/or to retrieve the secret. This library is not thread-safe.

The library expects the same configuration information in the manifest and environment variables as
RA-TLS. In addition, the library uses the following environment variables if available:

- ``SECRET_PROVISION_CONSTRUCTOR`` (optional) -- set it to ``1/true/TRUE`` to initialize the
  RA-TLS session and retrieve the secret before the application starts. By default, it is not set,
  thus secret provisioning must be explicitly requested by the application.

- ``SECRET_PROVISION_SET_PF_KEY`` (optional) -- set it to ``1/true/TRUE`` to indicate that the
  provisioned secret is a protected-files master key. The key must be a 32-char null-terminated
  AES-GCM encryption key in hex format, similar to ``sgx.protected_files_key`` manifest option.
  This environment variable is checked only if ``SECRET_PROVISION_CONSTRUCTOR`` is set.

- ``SECRET_PROVISION_SERVERS`` (optional) -- a comma, semicolon or space separated list of server
  names with ports to connect to for secret provisioning. Example:
  ``localhost:4433;trusted-server:443``. If not set, defaults to ``localhost:4433``.
  Alternatively, the application can specify it as an argument of ``secret_provision_start()``.

- ``SECRET_PROVISION_CA_CHAIN_PATH`` (required) -- a path to the CA chain of certificates to verify
  the server. Alternatively, the application can specify it as an argument of
  ``secret_provision_start()``.

The secret may be retrieved by the application in two ways:

- Reading ``SECRET_PROVISION_SECRET_STRING`` environment variable. It is updated only if
  ``SECRET_PROVISION_CONSTRUCTOR`` is set to true and if the secret is representable as a string of
  maximum 4K characters.
- Calling ``secret_provision_get()`` function. It always updates its pointer argument to the secret
  (or ``NULL`` if secret provisioning failed).

``secret_prov_verify_epid.so``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This library is typically linked into a (normal non-enclavized) secret-provisioning server service.
The server calls into this library to listen for clients in an endless loop. When a new client
connects, the server initiates an RA-TLS session with the client, verifies the RA-TLS X.509
certificate of the client, and provisions the secret to the client if verification is successful.
The server can register a callback to continue secure communication with the client (instead of
simply closing the session after the first secret is sent to the client).  This library is not
thread-safe. This library uses EPID-based RA-TLS flows underneath.

The library expects the same configuration information in the manifest and environment variables as
RA-TLS.

``secret_prov_verify_dcap.so``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Similarly to ``secret_prov_verify_epid.so``, this library is used in secret-provisioning servers.
The only difference is that this library uses ECDSA/DCAP-based RA-TLS flows underneath.

The library uses the same SGX-specific environment variables as ``secret_prov_verify_epid.so`` and
ignores the EPID-specific environment variables. The library expects all the DCAP infrastructure
to be installed and working correctly on the host.
