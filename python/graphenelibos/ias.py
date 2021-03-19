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
# - different input formats (hex, raw, base64?)
# - Report.raise_for_status(*, accept_adv=frozendict())

import base64
import enum
import posixpath
import pprint

import click
import requests

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
    def __init__(self, request_id, *, headers=None, data=None):
        self.headers = headers
        self.data = data

        self.request_id = request_id
        self.quote_status = QuoteStatus[data['isvEnclaveQuoteStatus']]

    @classmethod
    def from_resp(cls, resp):
        data = resp.json()
        return cls(
            request_id=resp.headers['request-id'],
            headers=dict(resp.headers),
            data=data)

    def raise_for_status(self, *, accept_advisories=frozenset()):
        raise NotImplementedError()

class IAS:
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
            return Report.from_resp(resp)

@click.command()
@click.option('--key', required=True)
@click.option('--nonce')
@click.argument('quote', type=click.File('r'))
def main(key, nonce, quote):
    ias = IAS(key)
    quote = bytes.fromhex(quote.read())

    report = ias.get_report(quote, nonce=nonce)
    click.echo(f'headers:\n{pprint.pformat(report.headers)}')
    click.echo(f'body:\n{pprint.pformat(report.data)}')
    click.echo(f'quote status: {report.quote_status}')

if __name__ == '__main__':
    # pylint: disable=no-value-for-parameter
    main()
