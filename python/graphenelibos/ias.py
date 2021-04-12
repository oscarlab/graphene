#!/usr/bin/env python3

'''
Tools for querying IAS v4 API.

.. seealso::

    API Documentation
        https://api.trustedservices.intel.com/documents/sgx-attestation-api-spec.pdf

    Explanation of SAs
        https://community.intel.com/t5/Intel-Software-Guard-Extensions/How-to-mitigate-common-SAs-reported-by-IAS-during-remote/m-p/1211599#M3985

    Intel SGX Technical Details for INTEL-SA-00289 and INTEL-SA-00334
        This concerns interpretation of ``CONFIGURATION*_NEEDED`` vs
        ``GROUP_OUT_OF_DATE``.
        https://cdrdv2.intel.com/v1/dl/getContent/619320
'''

# TODO
# - signature checking
# - sigrl endpoint
# - Report.raise_for_status(*, accept_adv=frozenset())

import base64
import binascii
import enum
import posixpath
import pprint

import click
import requests

from . import __version__ as _VERSION

class _APIStr(str):
    def __truediv__(self, other):
        return type(self)(posixpath.join(self, other.lstrip('/')))

API_DEV = _APIStr('https://api.trustedservices.intel.com/sgx/dev')
API = _APIStr('https://api.trustedservices.intel.com/sgx')

class _APIEnum(str, enum.Enum):
    # pylint: disable=no-self-argument,unused-argument
    def _generate_next_value_(name, start, count, last_value):
        return name

class ManifestStatus(_APIEnum):
    (
        OK, UNKNOWN, INVALID, OUT_OF_DATE, REVOKED, RL_VERSION_MISMATCH,
    ) = (enum.auto() for _ in range(6))

class QuoteStatus(_APIEnum):
    (
        OK, SIGNATURE_INVALID, GROUP_REVOKED, SIGNATURE_REVOKED, KEY_REVOKED,
        SIGRL_VERSION_MISMATCH, GROUP_OUT_OF_DATE, CONFIGURATION_NEEDED,
        SW_HARDENING_NEEDED, CONFIGURATION_AND_SW_HARDENING_NEEDED,
    ) = (enum.auto() for _ in range(10))

class Report:
    def __init__(self, request_id, *, headers=None, data=None, quote_status=None):
        self.request_id = request_id
        self.headers = headers
        self.data = data

        self.quote_status = quote_status

    @classmethod
    def from_resp_v4(cls, resp):
        data = resp.json()
        return cls(
            request_id=resp.headers['request-id'],
            headers=resp.headers,
            data=data,
            quote_status=QuoteStatus[data['isvEnclaveQuoteStatus']],
        )

class IASv4:
    def __init__(self, key, prod=False):
        self.headers = {'Ocp-Apim-Subscription-Key': key}
        self.api = API if prod else API_DEV

    def get_report(self, quote, manifest=None, nonce=None) -> Report:
        data = {'isvEnclaveQuote': base64.b64encode(quote)}
        if manifest is not None:
            data['manifest'] = manifest
        if nonce is not None:
            data['nonce'] = nonce

        with requests.post(self.api / 'attestation/v4/report',
                json=data, headers=self.headers) as resp:
            resp.raise_for_status()
            return Report.from_resp_v4(resp)

_FORMAT = {
    'raw': lambda data: data,
    'hex': binascii.unhexlify,
    'base64': base64.b64decode,
}

@click.command()
@click.option('--key', required=True,
    help='API key (Ocp-Apim-Subscription-Key header).')
@click.option('--format', 'fmt',
    type=click.Choice(_FORMAT), default='raw', show_default=True,
    help='Input format of the quote file.')
@click.option('--nonce',
    help='nonce (nonce JSON field).')
@click.version_option(_VERSION)
@click.argument('quote', metavar='FILE', type=click.File('rb'))
def main(key, fmt, nonce, quote):
    '''
    Send an SGX quote from the FILE to the Intel Attestation Service and get the
    SGX attestation report back.
    '''
    ias = IASv4(key)
    quote = _FORMAT[fmt](quote.read())

    report = ias.get_report(quote, nonce=nonce)
    click.echo(f'headers:\n{pprint.pformat(dict(report.headers))}')
    click.echo(f'body:\n{pprint.pformat(report.data)}')
    click.echo(f'quote status: {report.quote_status}')

if __name__ == '__main__':
    # pylint: disable=no-value-for-parameter
    main()
