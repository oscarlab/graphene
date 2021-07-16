#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later */
# Copyright (C) 2014 Stony Brook University
# Copyright (C) 2021 Intel Corporation
#                    Michał Kowalczyk <mkow@invisiblethingslab.com>

import argparse
import datetime
import hashlib
import os
from pathlib import Path
import struct
import subprocess
from sys import stderr

import toml

from . import _CONFIG_PKGLIBDIR
from . import _offsets as offs # pylint: disable=import-error,no-name-in-module

class ManifestError(Exception):
    pass


# pylint: enable=invalid-name

# Default / Architectural Options

ARCHITECTURE = 'amd64'

DEFAULT_ENCLAVE_SIZE = '256M'
DEFAULT_THREAD_NUM = 4

# Utilities

ZERO_PAGE = bytes(offs.PAGESIZE)


def roundup(addr):
    remaining = addr % offs.PAGESIZE
    if remaining:
        return addr + (offs.PAGESIZE - remaining)
    return addr


def rounddown(addr):
    return addr - addr % offs.PAGESIZE


def parse_size(value):
    scale = 1
    if value.endswith('K'):
        scale = 1024
    if value.endswith('M'):
        scale = 1024 * 1024
    if value.endswith('G'):
        scale = 1024 * 1024 * 1024
    if scale != 1:
        value = value[:-1]
    return int(value, 0) * scale


def exec_sig_manifest(args):
    if (args['mode'] != 'gendata'):
        sigfile = args['output']
        for ext in ['.manifest.sgx.d', '.manifest.sgx', '.manifest']:
            if sigfile.endswith(ext):
                sigfile = sigfile[:-len(ext)]
                break
        args['sigfile'] = sigfile + '.sig'

    if args.get('libpal', None) is None:
        print('Option --libpal must be given', file=stderr)
        return 1

    return 0


def output_manifest(filename, manifest):
    with open(filename, 'w', encoding='UTF-8') as file:
        file.write('# DO NOT MODIFY. THIS FILE WAS AUTO-GENERATED.\n\n')
        toml.dump(manifest, file)


def or_bytes(bytes_a, bytes_b):
    return bytes([a | b for a, b in zip(bytes_a, bytes_b)])


# Loading Enclave Attributes


def get_enclave_attributes(manifest):
    sgx_flags = {
        'FLAG_DEBUG': struct.pack('<Q', offs.SGX_FLAGS_DEBUG),
        'FLAG_MODE64BIT': struct.pack('<Q', offs.SGX_FLAGS_MODE64BIT),
    }

    sgx_xfrms = {
        'XFRM_LEGACY': struct.pack('<Q', offs.SGX_XFRM_LEGACY),
        'XFRM_AVX': struct.pack('<Q', offs.SGX_XFRM_AVX),
        'XFRM_AVX512': struct.pack('<Q', offs.SGX_XFRM_AVX512),
        'XFRM_MPX': struct.pack('<Q', offs.SGX_XFRM_MPX),
        'XFRM_PKRU': struct.pack('<Q', offs.SGX_XFRM_PKRU),
    }

    sgx_miscs = {
        'MISC_EXINFO': struct.pack('<L', offs.SGX_MISCSELECT_EXINFO),
    }

    manifest_options = [
        ('debug', 'FLAG_DEBUG'),
        ('require_avx', 'XFRM_AVX'),
        ('require_avx512', 'XFRM_AVX512'),
        ('require_mpx', 'XFRM_MPX'),
        ('require_pkru', 'XFRM_PKRU'),
        ('support_exinfo', 'MISC_EXINFO'),
    ]

    attributes = {'XFRM_LEGACY'} # this one always needs to be set in SGX (it means "SSE supported")
    if ARCHITECTURE == 'amd64':
        attributes.add('FLAG_MODE64BIT')

    for opt, flag in manifest_options:
        if manifest['sgx'][opt] == 1:
            attributes.add(flag)

    flags_raw = struct.pack('<Q', 0)
    xfrms_raw = struct.pack('<Q', 0)
    miscs_raw = struct.pack('<L', 0)

    for attr in attributes:
        if attr in sgx_flags:
            flags_raw = or_bytes(flags_raw, sgx_flags[attr])
        if attr in sgx_xfrms:
            xfrms_raw = or_bytes(xfrms_raw, sgx_xfrms[attr])
        if attr in sgx_miscs:
            miscs_raw = or_bytes(miscs_raw, sgx_miscs[attr])

    return flags_raw, xfrms_raw, miscs_raw


# Generate Checksums / Measurement

def resolve_uri(uri, check_exist=True):
    if not uri.startswith('file:'):
        raise ManifestError(f'Unsupported URI type: {uri}')
    path = Path(uri[len('file:'):])
    if check_exist and not path.exists():
        raise ManifestError(f'Cannot resolve {uri} or the file does not exist.')
    return str(path)


def sha256(data):
    sha = hashlib.sha256()
    sha.update(data)
    return sha.digest()


def get_hash(filename):
    with open(filename, 'rb') as file:
        return sha256(file.read())


# TODO: this function should be deleted after we start using TOML lists instead of key-values for
# trusted files.
def path_to_key(path):
    # anything which is unique to the path should do the work
    return sha256(path.encode()).hex()


def walk_dir(path):
    return sorted(filter(Path.is_file, path.rglob('*')))


def get_trusted_files(manifest, check_exist=True, do_hash=True):
    targets = {}

    preload_str = manifest['loader']['preload']
    # `filter` below is needed for the case where preload_str == '' (`split` returns [''] then)
    for i, uri in enumerate(filter(None, preload_str.split(','))):
        targets[f'preload{i}'] = uri, resolve_uri(uri, check_exist)

    for key, val in manifest['sgx']['trusted_files'].items():
        path = Path(resolve_uri(val, check_exist))
        if path.is_dir():
            for sub_path in walk_dir(path):
                sub_key = path_to_key(str(sub_path))
                uri = f'file:{sub_path}'
                targets[sub_key] = uri, sub_path
        else:
            targets[key] = val, path

    if do_hash:
        for key, val in targets.items():
            uri, target = val
            hash_ = get_hash(target).hex()
            targets[key] = uri, target, hash_

    return targets


# Populate Enclave Memory

PAGEINFO_R = 0x1
PAGEINFO_W = 0x2
PAGEINFO_X = 0x4
PAGEINFO_TCS = 0x100
PAGEINFO_REG = 0x200


def get_loadcmds(elf_filename):
    loadcmds = []
    proc = subprocess.Popen(['readelf', '-l', '-W', elf_filename],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    while True:
        line = proc.stdout.readline()
        if not line:
            break
        line = line.decode()
        stripped = line.strip()
        if not stripped.startswith('LOAD'):
            continue
        tokens = stripped.split()
        if len(tokens) < 6:
            continue
        if len(tokens) >= 7 and tokens[7] == 'E':
            tokens[6] += tokens[7]
        prot = 0
        for token in tokens[6]:
            if token == 'R':
                prot = prot | 4
            if token == 'W':
                prot = prot | 2
            if token == 'E':
                prot = prot | 1

        loadcmds.append((int(tokens[1][2:], 16),  # offset
                         int(tokens[2][2:], 16),  # addr
                         int(tokens[4][2:], 16),  # filesize
                         int(tokens[5][2:], 16),  # memsize
                         prot))
    proc.wait()
    if proc.returncode != 0:
        raise RuntimeError(f'Parsing {elf_filename} as ELF failed')
    return loadcmds


class MemoryArea:
    # pylint: disable=too-few-public-methods,too-many-instance-attributes
    def __init__(self, desc, elf_filename=None, content=None, addr=None, size=None,
                 flags=None, measure=True):
        # pylint: disable=too-many-arguments
        self.desc = desc
        self.elf_filename = elf_filename
        self.content = content
        self.addr = addr
        self.size = size
        self.flags = flags
        self.measure = measure

        if elf_filename:
            loadcmds = get_loadcmds(elf_filename)
            mapaddr = 0xffffffffffffffff
            mapaddr_end = 0
            for (_, addr_, _, memsize, _) in loadcmds:
                if rounddown(addr_) < mapaddr:
                    mapaddr = rounddown(addr_)
                if roundup(addr_ + memsize) > mapaddr_end:
                    mapaddr_end = roundup(addr_ + memsize)

            self.size = mapaddr_end - mapaddr
            if mapaddr > 0:
                self.addr = mapaddr

        if self.addr is not None:
            self.addr = rounddown(self.addr)
        if self.size is not None:
            self.size = roundup(self.size)


def get_memory_areas(attr, args):
    areas = []
    areas.append(
        MemoryArea('ssa',
                   size=attr['thread_num'] * offs.SSA_FRAME_SIZE * offs.SSA_FRAME_NUM,
                   flags=PAGEINFO_R | PAGEINFO_W | PAGEINFO_REG))
    areas.append(MemoryArea('tcs', size=attr['thread_num'] * offs.TCS_SIZE,
                            flags=PAGEINFO_TCS))
    areas.append(MemoryArea('tls', size=attr['thread_num'] * offs.PAGESIZE,
                            flags=PAGEINFO_R | PAGEINFO_W | PAGEINFO_REG))

    for _ in range(attr['thread_num']):
        areas.append(MemoryArea('stack', size=offs.ENCLAVE_STACK_SIZE,
                                flags=PAGEINFO_R | PAGEINFO_W | PAGEINFO_REG))
    for _ in range(attr['thread_num']):
        areas.append(MemoryArea('sig_stack', size=offs.ENCLAVE_SIG_STACK_SIZE,
                                flags=PAGEINFO_R | PAGEINFO_W | PAGEINFO_REG))

    areas.append(MemoryArea('pal', elf_filename=args['libpal'], flags=PAGEINFO_REG))
    return areas


def find_areas(areas, desc):
    return [area for area in areas if area.desc == desc]


def find_area(areas, desc, allow_none=False):
    matching = find_areas(areas, desc)

    if not matching and allow_none:
        return None

    if len(matching) != 1:
        raise KeyError(f'Could not find exactly one MemoryArea {desc!r}')

    return matching[0]


def entry_point(elf_path):
    env = os.environ
    env['LC_ALL'] = 'C'
    out = subprocess.check_output(
        ['readelf', '-l', '--', elf_path], env=env)
    for line in out.splitlines():
        line = line.decode()
        if line.startswith('Entry point '):
            return int(line[12:], 0)
    raise ValueError('Could not find entry point of elf file')


def gen_area_content(attr, areas, enclave_base, enclave_heap_min):
    # pylint: disable=too-many-locals
    manifest_area = find_area(areas, 'manifest')
    pal_area = find_area(areas, 'pal')
    ssa_area = find_area(areas, 'ssa')
    tcs_area = find_area(areas, 'tcs')
    tls_area = find_area(areas, 'tls')
    stacks = find_areas(areas, 'stack')
    sig_stacks = find_areas(areas, 'sig_stack')

    tcs_data = bytearray(tcs_area.size)

    def set_tcs_field(t, offset, pack_fmt, value):
        struct.pack_into(pack_fmt, tcs_data, t * offs.TCS_SIZE + offset, value)

    tls_data = bytearray(tls_area.size)

    def set_tls_field(t, offset, value):
        struct.pack_into('<Q', tls_data, t * offs.PAGESIZE + offset, value)

    enclave_heap_max = pal_area.addr

    # Sanity check that we measure everything except the heap which is zeroed
    # on enclave startup.
    for area in areas:
        if (area.addr + area.size <= enclave_heap_min or
                area.addr >= enclave_heap_max):
            if not area.measure:
                raise ValueError('Memory area, which is not the heap, is not measured')
        elif area.desc != 'free':
            raise ValueError('Unexpected memory area is in heap range')

    for t in range(0, attr['thread_num']):
        ssa = ssa_area.addr + offs.SSA_FRAME_SIZE * offs.SSA_FRAME_NUM * t
        ssa_offset = ssa - enclave_base
        set_tcs_field(t, offs.TCS_OSSA, '<Q', ssa_offset)
        set_tcs_field(t, offs.TCS_NSSA, '<L', offs.SSA_FRAME_NUM)
        set_tcs_field(t, offs.TCS_OENTRY, '<Q',
                      pal_area.addr + entry_point(pal_area.elf_filename) - enclave_base)
        set_tcs_field(t, offs.TCS_OGS_BASE, '<Q', tls_area.addr - enclave_base + offs.PAGESIZE * t)
        set_tcs_field(t, offs.TCS_OFS_LIMIT, '<L', 0xfff)
        set_tcs_field(t, offs.TCS_OGS_LIMIT, '<L', 0xfff)

        set_tls_field(t, offs.SGX_COMMON_SELF, tls_area.addr + offs.PAGESIZE * t)
        set_tls_field(t, offs.SGX_COMMON_STACK_PROTECTOR_CANARY,
                      offs.STACK_PROTECTOR_CANARY_DEFAULT)
        set_tls_field(t, offs.SGX_ENCLAVE_SIZE, attr['enclave_size'])
        set_tls_field(t, offs.SGX_TCS_OFFSET, tcs_area.addr - enclave_base + offs.TCS_SIZE * t)
        set_tls_field(t, offs.SGX_INITIAL_STACK_ADDR, stacks[t].addr + stacks[t].size)
        set_tls_field(t, offs.SGX_SIG_STACK_LOW, sig_stacks[t].addr)
        set_tls_field(t, offs.SGX_SIG_STACK_HIGH, sig_stacks[t].addr + sig_stacks[t].size)
        set_tls_field(t, offs.SGX_SSA, ssa)
        set_tls_field(t, offs.SGX_GPR, ssa + offs.SSA_FRAME_SIZE - offs.SGX_GPR_SIZE)
        set_tls_field(t, offs.SGX_MANIFEST_SIZE, len(manifest_area.content))
        set_tls_field(t, offs.SGX_HEAP_MIN, enclave_heap_min)
        set_tls_field(t, offs.SGX_HEAP_MAX, enclave_heap_max)

    tcs_area.content = tcs_data
    tls_area.content = tls_data


def populate_memory_areas(attr, areas, enclave_base, enclave_heap_min):
    last_populated_addr = enclave_base + attr['enclave_size']

    for area in areas:
        if area.addr is not None:
            continue

        area.addr = last_populated_addr - area.size
        if area.addr < enclave_heap_min:
            raise Exception('Enclave size is not large enough')
        last_populated_addr = area.addr

    free_areas = []
    for area in areas:
        addr = area.addr + area.size
        if addr < last_populated_addr:
            flags = PAGEINFO_R | PAGEINFO_W | PAGEINFO_X | PAGEINFO_REG
            free_areas.append(
                MemoryArea('free', addr=addr, size=last_populated_addr - addr,
                           flags=flags, measure=False))
            last_populated_addr = area.addr

    if last_populated_addr > enclave_heap_min:
        flags = PAGEINFO_R | PAGEINFO_W | PAGEINFO_X | PAGEINFO_REG
        free_areas.append(
            MemoryArea('free', addr=enclave_heap_min,
                       size=last_populated_addr - enclave_heap_min, flags=flags,
                       measure=False))

    gen_area_content(attr, areas, enclave_base, enclave_heap_min)

    return areas + free_areas

def generate_measurement(enclave_base, attr, areas):
    # pylint: disable=too-many-statements,too-many-branches,too-many-locals

    def do_ecreate(digest, size):
        data = struct.pack('<8sLQ44s', b'ECREATE', offs.SSA_FRAME_SIZE // offs.PAGESIZE, size, b'')
        digest.update(data)

    def do_eadd(digest, offset, flags):
        assert offset < attr['enclave_size']
        data = struct.pack('<8sQQ40s', b'EADD', offset, flags, b'')
        digest.update(data)

    def do_eextend(digest, offset, content):
        assert offset < attr['enclave_size']

        if len(content) != 256:
            raise ValueError('Exactly 256 bytes expected')

        data = struct.pack('<8sQ48s', b'EEXTEND', offset, b'')
        digest.update(data)
        digest.update(content)

    def include_page(digest, addr, flags, content, measure):
        if len(content) != offs.PAGESIZE:
            raise ValueError('Exactly one page expected')

        do_eadd(digest, addr - enclave_base, flags)
        if measure:
            for i in range(0, offs.PAGESIZE, 256):
                do_eextend(digest, addr - enclave_base + i, content[i:i + 256])

    mrenclave = hashlib.sha256()
    do_ecreate(mrenclave, attr['enclave_size'])

    def print_area(addr, size, flags, desc, measured):
        if flags & PAGEINFO_REG:
            type_ = 'REG'
        if flags & PAGEINFO_TCS:
            type_ = 'TCS'
        prot = ['-', '-', '-']
        if flags & PAGEINFO_R:
            prot[0] = 'R'
        if flags & PAGEINFO_W:
            prot[1] = 'W'
        if flags & PAGEINFO_X:
            prot[2] = 'X'
        prot = ''.join(prot)

        desc = f'({desc})'
        if measured:
            desc += ' measured'

        print(f'    {addr:016x}-{addr+size:016x} [{type_}:{prot}] {desc}')

    def load_file(digest, file, offset, addr, filesize, memsize, desc, flags):
        # pylint: disable=too-many-arguments
        f_addr = rounddown(offset)
        m_addr = rounddown(addr)
        m_size = roundup(addr + memsize) - m_addr

        print_area(m_addr, m_size, flags, desc, True)

        for page in range(m_addr, m_addr + m_size, offs.PAGESIZE):
            start = page - m_addr + f_addr
            end = start + offs.PAGESIZE
            start_zero = b''
            if start < offset:
                if offset - start >= offs.PAGESIZE:
                    start_zero = ZERO_PAGE
                else:
                    start_zero = bytes(offset - start)
            end_zero = b''
            if end > offset + filesize:
                if end - offset - filesize >= offs.PAGESIZE:
                    end_zero = ZERO_PAGE
                else:
                    end_zero = bytes(end - offset - filesize)
            start += len(start_zero)
            end -= len(end_zero)
            if start < end:
                file.seek(start)
                data = file.read(end - start)
            else:
                data = b''
            if len(start_zero + data + end_zero) != offs.PAGESIZE:
                raise Exception('wrong calculation')

            include_page(digest, page, flags, start_zero + data + end_zero, True)

    for area in areas:
        if area.elf_filename is not None:
            with open(area.elf_filename, 'rb') as file:
                loadcmds = get_loadcmds(area.elf_filename)
                if loadcmds:
                    mapaddr = 0xffffffffffffffff
                    for (offset, addr, filesize, memsize,
                         prot) in loadcmds:
                        if rounddown(addr) < mapaddr:
                            mapaddr = rounddown(addr)
                baseaddr_ = area.addr - mapaddr
                for (offset, addr, filesize, memsize, prot) in loadcmds:
                    flags = area.flags
                    if prot & 4:
                        flags = flags | PAGEINFO_R
                    if prot & 2:
                        flags = flags | PAGEINFO_W
                    if prot & 1:
                        flags = flags | PAGEINFO_X

                    if flags & PAGEINFO_X:
                        desc = 'code'
                    else:
                        desc = 'data'
                    load_file(mrenclave, file, offset, baseaddr_ + addr, filesize, memsize,
                              desc, flags)
        else:
            for addr in range(area.addr, area.addr + area.size, offs.PAGESIZE):
                data = ZERO_PAGE
                if area.content is not None:
                    start = addr - area.addr
                    end = start + offs.PAGESIZE
                    data = area.content[start:end]
                    data += b'\0' * (offs.PAGESIZE - len(data)) # pad last page
                include_page(mrenclave, addr, area.flags, data, area.measure)

            print_area(area.addr, area.size, area.flags, area.desc,
                       area.measure)

    return mrenclave.digest()


def generate_sigstruct(attr, args, mrenclave):
    '''Generate Sigstruct.

    field format: (offset, type, value)
    ''' # pylint: disable=too-many-locals

    fields = {
        'header': (offs.SGX_ARCH_ENCLAVE_CSS_HEADER,
                   '<4L', 0x00000006, 0x000000e1, 0x00010000, 0x00000000),
        'module_vendor': (offs.SGX_ARCH_ENCLAVE_CSS_MODULE_VENDOR, '<L', 0x00000000),
        'date': (offs.SGX_ARCH_ENCLAVE_CSS_DATE, '<HBB', attr['year'], attr['month'], attr['day']),
        'header2': (offs.SGX_ARCH_ENCLAVE_CSS_HEADER2,
                    '<4L', 0x00000101, 0x00000060, 0x00000060, 0x00000001),
        'hw_version': (offs.SGX_ARCH_ENCLAVE_CSS_HW_VERSION, '<L', 0x00000000),
        'misc_select': (offs.SGX_ARCH_ENCLAVE_CSS_MISC_SELECT, '4s', attr['misc_select']),
        'misc_mask': (offs.SGX_ARCH_ENCLAVE_CSS_MISC_MASK, '4s', attr['misc_select']),
        'attributes': (offs.SGX_ARCH_ENCLAVE_CSS_ATTRIBUTES, '8s8s', attr['flags'], attr['xfrms']),
        'attribute_mask': (offs.SGX_ARCH_ENCLAVE_CSS_ATTRIBUTE_MASK,
                           '8s8s', attr['flags'], attr['xfrms']),
        'enclave_hash': (offs.SGX_ARCH_ENCLAVE_CSS_ENCLAVE_HASH, '32s', mrenclave),
        'isv_prod_id': (offs.SGX_ARCH_ENCLAVE_CSS_ISV_PROD_ID, '<H', attr['isv_prod_id']),
        'isv_svn': (offs.SGX_ARCH_ENCLAVE_CSS_ISV_SVN, '<H', attr['isv_svn']),
    }

    sign_buffer = bytearray(128 + 128)

    for field in fields.values():
        if field[0] >= offs.SGX_ARCH_ENCLAVE_CSS_MISC_SELECT:
            struct.pack_into(field[1], sign_buffer,
                             field[0] - offs.SGX_ARCH_ENCLAVE_CSS_MISC_SELECT + 128,
                             *field[2:])
        else:
            struct.pack_into(field[1], sign_buffer, field[0], *field[2:])

    if (args['mode'] == 'gendata'):
        return sign_buffer
    elif (args['mode'] == 'catsig'):
        def read_bin_file(filename):
            fh = open(filename, 'rb')
            content = fh.read()
            fh.close()
            return content
        signature = read_bin_file(args['sig'])
        signature = signature[::-1]

        proc = subprocess.Popen(
            ['openssl', 'dgst', '-verify', args['key'], '-sha256', '-signature', args['sig'], args['unsigned']], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        result, _ = proc.communicate()
        if (str(result).find("Verified OK") == -1):
            return bytearray(0)

       # generate modulus from public key
        proc = subprocess.Popen(
            ['openssl', 'rsa', '-pubin', '-in', args['key'], '-modulus', '-noout'], stdout=subprocess.PIPE)
        modulus_out, _ = proc.communicate()

    else:  # sign
        # send sign_buffer to open ssl sha256 to generate otuput signature
        proc = subprocess.Popen(
            ['openssl', 'sha256', '-binary', '-sign', args['key']],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        signature, _ = proc.communicate(sign_buffer)
        signature = signature[::-1]

        # generate modulus from private key
        proc = subprocess.Popen(
            ['openssl', 'rsa', '-modulus', '-in', args['key'], '-noout'],
            stdout=subprocess.PIPE)
        modulus_out, _ = proc.communicate()

    modulus = bytes.fromhex(modulus_out[8:8+offs.SE_KEY_SIZE*2].decode())
    modulus = bytes(reversed(modulus))
    modulus_int = int.from_bytes(modulus, byteorder='little')
    signature_int = int.from_bytes(signature, byteorder='little')

    tmp1 = signature_int * signature_int
    q1_int = tmp1 // modulus_int
    tmp2 = tmp1 % modulus_int
    q2_int = tmp2 * signature_int // modulus_int

    q1 = q1_int.to_bytes(384, byteorder='little') # pylint: disable=invalid-name
    q2 = q2_int.to_bytes(384, byteorder='little') # pylint: disable=invalid-name

    fields.update({
        'modulus': (offs.SGX_ARCH_ENCLAVE_CSS_MODULUS, '384s', modulus),
        'exponent': (offs.SGX_ARCH_ENCLAVE_CSS_EXPONENT, '<L', 3),
        'signature': (offs.SGX_ARCH_ENCLAVE_CSS_SIGNATURE, '384s', signature),

        'q1': (offs.SGX_ARCH_ENCLAVE_CSS_Q1, '384s', q1),
        'q2': (offs.SGX_ARCH_ENCLAVE_CSS_Q2, '384s', q2),
    })

    buffer = bytearray(offs.SGX_ARCH_ENCLAVE_CSS_SIZE)

    for field in fields.values():
        #filed[1] is format, field[0] is offset
        struct.pack_into(field[1], buffer, field[0], *field[2:])
    return buffer


# Main Program

argparser = argparse.ArgumentParser()
argparser.add_argument('--output', '-output', metavar='OUTPUT',
                       type=str, required=True,
                       help='Output .manifest.sgx file '
                            '(manifest augmented with autogenerated fields)')
argparser.add_argument('--libpal', '-libpal', metavar='LIBPAL',
                       type=str,
                       help='Input libpal file (by default it gets the installed one)')
argparser.add_argument('--key', '-key', metavar='KEY',
                       type=str, required=False,
                       help='specify signing key(.pem) file')
argparser.add_argument('--manifest', '-manifest', metavar='MANIFEST',
                       type=str, required=True,
                       help='Input .manifest file '
                            '(user-prepared manifest template)')
argparser.add_argument('--depend', '-depend',
                       action='store_true', required=False,
                       help='Generate dependency for Makefile')
argparser.add_argument('--mode', '-mode',
                       type=str, required=False, default='sign',
                       help='mode is one of: sign, gendata, catsig')
argparser.add_argument('--sig', '-sig',
                       type=str, required=False,
                       help='the signature file for the signing material')
argparser.add_argument('--unsigned', '-unsigned',
                       type=str, required=False,
                       help='the signing material generated by gendata')

argparser.set_defaults(libpal=os.path.join(_CONFIG_PKGLIBDIR, 'sgx/libpal.so'))

def parse_args(args):
    args = argparser.parse_args(args)
    args_dict = {
        'output': args.output,
        'libpal': args.libpal,
        'key': args.key,
        'manifest': args.manifest,
        'mode' : args.mode,
    }
    if args.depend:
        args_dict['depend'] = True
    else:
        # key is required and not found in manifest
        if args.mode == 'catsig':
            if args.key is None:
                argparser.error("a key is required to sign")
                return None
    if args.mode == 'catsig':
        if args.sig is None:
            argparser.error("a signature file is required by catsig mode")
            return None
        else:
            args_dict['sig'] = args.sig
        if args.unsigned is None:
            argparser.error("an unsigned file is required by catsig mode")
            return None
        else:
            args_dict['unsigned'] = args.unsigned
    if args.output is None:
        argparser.error("an output file is required")
        return None
    else:
        args_dict['output'] = args.output
    return args_dict


def read_manifest(path):
    manifest = toml.load(path)

    # set defaults to simplify lookup code (otherwise we'd need to check keys existence each time)

    sgx = manifest.setdefault('sgx', {})
    sgx.setdefault('trusted_files', {})
    sgx.setdefault('trusted_checksum', {})
    sgx.setdefault('enclave_size', DEFAULT_ENCLAVE_SIZE)
    sgx.setdefault('thread_num', DEFAULT_THREAD_NUM)
    sgx.setdefault('isvprodid', 0)
    sgx.setdefault('isvsvn', 0)
    sgx.setdefault('remote_attestation', False)
    sgx.setdefault('debug', True)
    sgx.setdefault('require_avx', False)
    sgx.setdefault('require_avx512', False)
    sgx.setdefault('require_mpx', False)
    sgx.setdefault('require_pkru', False)
    sgx.setdefault('support_exinfo', False)
    sgx.setdefault('nonpie_binary', False)
    sgx.setdefault('enable_stats', False)

    loader = manifest.setdefault('loader', {})
    loader.setdefault('preload', '')

    return manifest


def main_sign(manifest, args):
    # pylint: disable=too-many-statements,too-many-branches,too-many-locals
    if exec_sig_manifest(args) != 0:
        return 1

    # Get attributes from manifest
    attr = {}

    manifest_sgx = manifest['sgx']

    attr['enclave_size'] = parse_size(manifest_sgx['enclave_size'])
    attr['thread_num'] = manifest_sgx['thread_num']
    attr['isv_prod_id'] = manifest_sgx['isvprodid']
    attr['isv_svn'] = manifest_sgx['isvsvn']
    attr['flags'], attr['xfrms'], attr['misc_select'] = get_enclave_attributes(manifest)
    today = datetime.date.today()
    attr['year'] = today.year
    attr['month'] = today.month
    attr['day'] = today.day

    print('Attributes:')
    print(f'    size:        {attr["enclave_size"]:#x}')
    print(f'    thread_num:  {attr["thread_num"]}')
    print(f'    isv_prod_id: {attr["isv_prod_id"]}')
    print(f'    isv_svn:     {attr["isv_svn"]}')
    print(f'    attr.flags:  {attr["flags"].hex()}')
    print(f'    attr.xfrm:   {attr["xfrms"].hex()}')
    print(f'    misc_select: {attr["misc_select"].hex()}')
    print(f'    date:        {attr["year"]:04d}-{attr["month"]:02d}-{attr["day"]:02d}')

    if manifest_sgx['remote_attestation']:
        spid = manifest_sgx.get('ra_client_spid', '')
        linkable = manifest_sgx.get('ra_client_linkable', False)
        print('SGX remote attestation:')
        if not spid:
            print('    DCAP/ECDSA')
        else:
            print(f'    EPID (spid = {spid}, linkable = {linkable})')

    # Get trusted hashes and measurements

    # Use `list()` to ensure non-laziness (`manifest_sgx` is a part of `manifest`, and we'll be
    # changing it while iterating).
    expanded_trusted_files = list(get_trusted_files(manifest).items())
    manifest_sgx['trusted_files'] = {} # generate the list from scratch, dropping directory entries
    for key, val in expanded_trusted_files:
        uri, _, hash_ = val
        manifest_sgx['trusted_files'][key] = uri
        manifest_sgx['trusted_checksum'][key] = hash_

    # Populate memory areas
    memory_areas = get_memory_areas(attr, args)

    if manifest_sgx['nonpie_binary']:
        enclave_base = offs.DEFAULT_ENCLAVE_BASE
        enclave_heap_min = offs.MMAP_MIN_ADDR
    else:
        enclave_base = attr['enclave_size']
        enclave_heap_min = enclave_base

    if (args['mode'] != 'gendata'):
        output_manifest(args['output'], manifest)

        with open(args['output'], 'rb') as file:
             manifest_data = file.read()
    else:
        output_manifest(args['output'], manifest)
        manifest_data_str = toml.dumps(manifest, None)
        comment = '# DO NOT MODIFY. THIS FILE WAS AUTO-GENERATED.\n\n'
        manifest_data = bytes(comment + manifest_data_str, 'utf-8')

    manifest_data += b'\0' # in-memory manifest needs NULL-termination
    memory_areas = [
        MemoryArea('manifest', content=manifest_data, size=len(manifest_data),
                   flags=PAGEINFO_R | PAGEINFO_REG)
        ] + memory_areas

    memory_areas = populate_memory_areas(attr, memory_areas, enclave_base, enclave_heap_min)

    print('Memory:')
    # Generate measurement
    mrenclave = generate_measurement(enclave_base, attr, memory_areas)
    print('Measurement:')
    print(f'    {mrenclave.hex()}')

    # Generate sigstruct
    if (args['mode'] == 'gendata'):
        fileout = args['output']
    else:
        fileout = args['sigfile']
    gen_sig = generate_sigstruct(attr, args, mrenclave)
    if len(str(gen_sig)) > 0:
        f_sig = open(fileout, 'wb')
        f_sig.write(gen_sig)
        f_sig.close()
    return 0


def make_depend(manifest, args):
    output = args['output']

    if exec_sig_manifest(args) != 0:
        return 1

    dependencies = set()
    for _, filename in get_trusted_files(manifest, check_exist=False, do_hash=False).values():
        dependencies.add(filename)
    if args['libpal'] is not None:
        dependencies.add(args['libpal'])
    dependencies.add(args['key'])

    with open(output, 'w', encoding='UTF-8') as file:
        manifest_sgx = output
        if manifest_sgx.endswith('.d'):
            manifest_sgx = manifest_sgx[:-len('.d')]
        file.write(f'{manifest_sgx} {args["sigfile"]}:')
        for filename in dependencies:
            file.write(f' \\\n\t{filename}')
        file.write('\n')

    return 0


def main(args=None):
    args = parse_args(args)
    if args is None:
        return 1

    manifest_path = args['manifest']
    try:
        manifest = read_manifest(manifest_path)
    except toml.TomlDecodeError as exc:
        print(f'Parsing {manifest_path} as TOML failed: {exc}', file=stderr)
        return 1

    if args.get('depend'):
        return make_depend(manifest, args)
    return main_sign(manifest, args)
