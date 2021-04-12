.. program:: ias_request

=====================================================================
:program:`ias_request` -- Submit Intel Attestation Service v3 request
=====================================================================

Synopsis
========

:command:`ias_request` *COMMAND* [*OPTION*]...

Description
===========

`ias_request` submits requests to Intel Attestation Service (IAS) using
(obsolete) v3 API. Possible commands are retrieving EPID signature revocation
list and verifying attestation evidence for an SGX enclave quote.

Command line arguments
======================

General options
---------------

.. option:: -h, --help

   Display usage.

.. option:: -v, --verbose

   Print more information.

.. option:: -m, --msb

   Print/parse hex strings in big-endian order.

.. option:: -k, --api-key

   IAS API key.

Commands
--------

.. option:: sigrl

   Retrieve signature revocation list for a given EPID group.

   Possible ``sigrl`` options:

   .. option:: -g, --gid

      EPID group id (hex string).

   .. option:: -i, --sigrl-path

      Path to save retrieved SigRL to.

   .. option:: -S, --sigrl-url

      URL for the IAS SigRL endpoint (optional).

.. option:: report

   Verify attestation evidence (quote).

   Possible ``report`` options:

   .. option:: -q, --quote-path

      Path to quote to submit.

   .. option:: -r, --report-path

      Path to save IAS report to.

   .. option:: -s, --sig-path

      Path to save IAS report's signature to.

   .. option:: -n, --nonce

      Nonce to use (optional).

   .. option:: -c, --cert-path

      Path to save IAS certificate to (optional).

   .. option:: -a, --advisory-path

      Path to save IAS security advisories to (optional).

   .. option:: -R, --report-url

      URL for the IAS attestation report endpoint (optional).
