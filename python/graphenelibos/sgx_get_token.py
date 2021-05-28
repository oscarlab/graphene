#!/usr/bin/env python3
# pylint: disable=invalid-name,wrong-import-position

import argparse
import array
import hashlib
import socket
import struct
import sys

from . import aesm_pb2 # pylint: disable=no-name-in-module

try:
    from . import _offsets as offs # pylint: disable=import-error
except ImportError:
    # when we're in repo, _offsets does not exist and pal-sgx-sign sets sys.path
    # so we can import as follows
    import generated_offsets as offs # pylint: disable=import-error

# pylint: enable=invalid-name

def set_optional_sgx_features(attr):
    '''Set optional SGX features if they are available on this machine.'''
    optional_sgx_features = {
        offs.SGX_XFRM_AVX:      'avx',
        offs.SGX_XFRM_AVX512:   'avx512f',
        offs.SGX_XFRM_MPX:      'mpx',
        offs.SGX_XFRM_PKRU:     'pku', # "pku" is not a typo, that's how cpuinfo reports it
    }

    cpu_features = ''
    with open('/proc/cpuinfo', 'r') as file:
        for line in file:
            if line.startswith('flags'):
                cpu_features = line.split(':')[1].strip().split()
                break
        else:
            raise Exception('Failed to parse CPU flags')

    xfrms = int.from_bytes(attr['xfrms'], byteorder='little')
    xfrmmask = int.from_bytes(attr['xfrm_mask'], byteorder='little')

    new_xfrms = 0
    for (bits, feature) in optional_sgx_features.items():
        # Check if SIGSTRUCT allows enabling an optional CPU feature.
        # If all the xfrm bits for a feature, after applying xfrmmask, are set in xfrms,
        # we can set the remaining bits if the feature is available.
        # If the xfrmmask includes all the required xfrm bits, then these bits cannot be
        # changed in xfrm (need to stay the same as signed).
        if xfrms & (bits & xfrmmask) == (bits & xfrmmask) and feature in cpu_features:
            new_xfrms |= xfrms | bits

    attr['xfrms'] = new_xfrms.to_bytes(length=8, byteorder='little')


def read_sigstruct(sig):
    '''Reading Sigstruct.'''
    # field format: (offset, type, value)
    # SGX_ARCH_ENCLAVE_CSS_
    fields = {
        'date': (offs.SGX_ARCH_ENCLAVE_CSS_DATE, '<HBB', 'year', 'month', 'day'),
        'modulus': (offs.SGX_ARCH_ENCLAVE_CSS_MODULUS, '384s', 'modulus'),
        'exponent': (offs.SGX_ARCH_ENCLAVE_CSS_EXPONENT, '<L', 'exponent'),
        'signature': (offs.SGX_ARCH_ENCLAVE_CSS_SIGNATURE, '384s', 'signature'),

        'misc_select': (offs.SGX_ARCH_ENCLAVE_CSS_MISC_SELECT, '4s', 'misc_select'),
        'misc_mask': (offs.SGX_ARCH_ENCLAVE_CSS_MISC_MASK, '4s', 'misc_mask'),
        'attributes': (offs.SGX_ARCH_ENCLAVE_CSS_ATTRIBUTES, '8s8s', 'flags', 'xfrms'),
        'attribute_mask': (offs.SGX_ARCH_ENCLAVE_CSS_ATTRIBUTE_MASK, '8s8s',
            'flag_mask', 'xfrm_mask'),
        'enclave_hash': (offs.SGX_ARCH_ENCLAVE_CSS_ENCLAVE_HASH, '32s', 'enclave_hash'),
        'isv_prod_id': (offs.SGX_ARCH_ENCLAVE_CSS_ISV_PROD_ID, '<H', 'isv_prod_id'),
        'isv_svn': (offs.SGX_ARCH_ENCLAVE_CSS_ISV_SVN, '<H', 'isv_svn'),
    }

    attr = dict()
    for field in fields.values():
        values = struct.unpack_from(field[1], sig, field[0])

        for i, value in enumerate(values):
            attr[field[i + 2]] = value

    return attr

def is_dcap():
    '''Check if we're dealing with DCAP driver.'''
    return hasattr(offs, 'SGX_DCAP')

def connect_aesmd(attr):
    '''Connect with AESMD.'''

    req_msg = aesm_pb2.GetTokenReq()
    req_msg.req.signature = attr['enclave_hash']
    req_msg.req.key = attr['modulus']
    req_msg.req.attributes = attr['flags'] + attr['xfrms']
    req_msg.req.timeout = 10000

    req_msg_raw = req_msg.SerializeToString()

    aesm_service = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

    # Try to connect to all possible interfaces exposed by aesm service
    connections = (
        '/var/run/aesmd/aesm.socket',         # named socket (for PSW 1.8+)
        '\0sgx_aesm_socket_base' + '\0' * 87  # unnamed socket (for PSW 1.6/1.7)
    )

    for conn in connections:
        try:
            aesm_service.connect(conn)
        except socket.error:
            continue
        break
    else:
        raise socket.error('Cannot connect to the AESMD service')

    aesm_service.send(struct.pack('<I', len(req_msg_raw)))
    aesm_service.send(req_msg_raw)

    ret_msg_size = struct.unpack('<I', aesm_service.recv(4))[0]
    ret_msg = aesm_pb2.GetTokenRet()
    ret_msg_raw = aesm_service.recv(ret_msg_size)
    ret_msg.ParseFromString(ret_msg_raw)

    if ret_msg.ret.error != 0:
        raise Exception(f'Failed. (Error Code = {ret_msg.ret.error})')

    return ret_msg.ret.token

def create_dummy_token(attr):
    '''
    Create dummy token with a few fields initialized with real values and others
    with a placeholder ('\\0')
    '''
    token = array.array('B', b'\0'*304)

    # format: field_name -> tuple (offset, type_with_size)
    fields = dict()

    fields['valid'] = (0, '<I')
    fields['reserved'] = (4, '44B')
    fields['flags'] = (48, '<Q') # attrs
    fields['xfrms'] = (56, '<Q') # attrs
    fields['mrenclave'] = (64, '32B')
    fields['reserved2'] = (96, '32B')
    fields['mrsigner'] = (128, '32B')
    fields['reserved3'] = (160, '32B')
    fields['cpusvnle'] = (192, '<2Q')
    fields['isvprodidle'] = (208, '<H')
    fields['isvsvnle'] = (210, '<H')
    fields['reserved4'] = (212, '24B')
    fields['misc_mask'] = (236, '<I')
    fields['flagmask'] = (240, '<Q') # attrmask
    fields['xfrmmask'] = (248, '<Q') # attrmask
    fields['keyid'] = (256, '32B')
    fields['mac'] = (288, '16B')

    # fields read by create_enclave() in sgx_framework.c
    actual_fields = ['flags', 'xfrms', 'misc_mask']

    for key in actual_fields:
        field = fields[key]
        field_size = struct.Struct(field[1]).size
        token[field[0]:field[0] + field_size] = array.array('B', attr[key])

    return token

argparser = argparse.ArgumentParser()
argparser.add_argument('--sig', '-sig', metavar='SIGNATURE',
                       type=argparse.FileType('rb'), required=True,
                       help='Input .sig file (contains SIGSTRUCT)')
argparser.add_argument('--output', '-output', metavar='OUTPUT',
                       type=argparse.FileType('wb'), required=False,
                       help='Output .token file (contains EINITTOKEN)')


def main(args=None):
    '''Main Program.'''
    args = argparser.parse_args(args)

    attr = read_sigstruct(args.sig.read())
    set_optional_sgx_features(attr)

    # calculate MRSIGNER as sha256 hash over RSA public key's modulus
    mrsigner = hashlib.sha256()
    mrsigner.update(attr['modulus'])

    print('Attributes:')
    print(f'    mr_enclave:  {attr["enclave_hash"].hex()}')
    print(f'    mr_signer:   {mrsigner.digest().hex()}')
    print(f'    isv_prod_id: {attr["isv_prod_id"]}')
    print(f'    isv_svn:     {attr["isv_svn"]}')
    print(f'    attr.flags:  {int.from_bytes(attr["flags"], byteorder="big"):016x}')
    print(f'    attr.xfrm:   {int.from_bytes(attr["xfrms"], byteorder="big"):016x}')
    print(f'    misc_select: {int.from_bytes(attr["misc_select"], byteorder="big"):08x}')
    print(f'    misc_mask:   {int.from_bytes(attr["misc_mask"], byteorder="big"):08x}')
    print(f'    modulus:     {attr["modulus"].hex()[:32]}...')
    print(f'    exponent:    {attr["exponent"]}')
    print(f'    signature:   {attr["signature"].hex()[:32]}...')
    print(f'    date:        {attr["year"]:04d}-{attr["month"]:02d}-{attr["day"]:02d}')

    if is_dcap():
        token = create_dummy_token(attr)
    else:
        token = connect_aesmd(attr)

    if args.output:
        args.output.write(token)
    return 0


if __name__ == '__main__':
    sys.exit(main())
