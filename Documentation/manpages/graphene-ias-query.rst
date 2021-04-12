.. program:: graphene-ias-query

===============================================================================
:program:`graphene-ias-query` -- Submit Intel Attestation Service APIv4 request
===============================================================================

Synopsis
========

:command:`graphene-ias-query` [*OPTIONS*] *FILE*

Description
===========

:program:`graphene-ias-query` is a |~| quick-and-dirty program which queries IAS
v4 API (currently only development endpoint) and formats the response. No
signature checking is performed.

The quote is to be provided in *FILE* (``-`` means standard input).

Command line arguments
======================

.. option:: --key <key>

   API key, which will be sent as HTTP header ``Ocp-Apim-Subscription-Key:``.
   Mandatory.

.. option:: --nonce <nonce>

   Nonce, which will be sent as ``nonce`` field in JSON payload. If not
   provided, no nonce will be sent.

.. option:: --format <format>

   Encoding in which the quote is provided. One of ``raw`` (the default),
   ``hex``, ``base64``.

.. option:: --help

   Display usage and exit.

.. option:: --version

   Display version and exit.

Python API
==========

The functionality is available as Python API:

.. code-block:: python

   from graphenelibos import ias

   api = ias.APIv4(key=...)
   report = api.get_report(quote=open('quote', 'rb').read())
   print(report.quote_status)
   assert report.quote_status == ias.QuoteStatus.OK
