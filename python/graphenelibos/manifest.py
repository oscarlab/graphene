# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2021 Wojtek Porczyk <woju@invisiblethingslab.com>
# Copyright (c) 2020 Intel Corporation
#                    Michał Kowalczyk <mkow@invisiblethingslab.com>

'''
Graphene manifest renderer
'''

import os
import pathlib
import subprocess
import sys
import sysconfig

import click
import jinja2

from . import (
    _CONFIG_PKGLIBDIR,
)

def ldd(*args):
    '''
    Args:
        binaries for which to generate manifest trusted files list.
    '''
    # Be careful: We have to skip vdso, which doesn't have a corresponding file on the disk (we
    # assume that such files have paths starting with '/', seems ldd always prints absolute paths).
    # Also, old ldd (from Ubuntu 16.04) prints vdso differently than newer ones:
    # old:
    #     linux-vdso.so.1 =>  (0x00007ffd31fee000)
    # new:
    #     linux-vdso.so.1 (0x00007ffd31fee000)
    ret = set()
    for line in subprocess.check_output(['ldd', *(os.fspath(i) for i in args)]).decode('ascii'):
        line = line.strip().split()
        if line[1] == '=>' and line[2].startswith('/'):
            ret.add(line[2])
        elif line[0].startswith('/') and line[1].startswith('/'):
            ret.add(line[0])
    return sorted(ret)

def get_distlib():
    ''' Get distlib directory depending on distro. Examples are:
    - Ubuntu: /usr/lib/python3/dist-packages
    - Fedora: /usr/lib64/python3.9/site-packages
    '''
    bases = [
        pathlib.Path(sysconfig.get_path('stdlib',
                                        vars={'py_version_short': sys.version_info[0]})),
        sysconfig.get_path('stdlib')
    ]
    for base in bases:
        for directory in ['dist-packages', 'site-packages']:
            p = os.path.join(base, directory)
            if os.path.isdir(p):
                return p
    return ''

def add_globals_from_python(env):
    paths = sysconfig.get_paths()
    env.globals['python'] = {
        'stdlib': pathlib.Path(paths['stdlib']),
        'platstdlib': pathlib.Path(paths['platstdlib']),
        'purelib': pathlib.Path(paths['purelib']),

        # TODO rpm-based distros
        'distlib': get_distlib(),

        'get_config_var': sysconfig.get_config_var,
        'ext_suffix': sysconfig.get_config_var('EXT_SUFFIX'),

        'get_path': sysconfig.get_path,
        'get_paths': sysconfig.get_paths,

        'implementation': sys.implementation,
    }

class Runtimedir:
    @staticmethod
    def __call__(libc='glibc'):
        return (pathlib.Path(_CONFIG_PKGLIBDIR) / 'runtime' / libc).resolve()
    def __str__(self):
        return str(self())
    def __truediv__(self, other):
        return self() / other

def add_globals_from_graphene(env):
    env.globals['graphene'] = {
        'libos': pathlib.Path(_CONFIG_PKGLIBDIR) / 'libsysdb.so',
        'pkglibdir': pathlib.Path(_CONFIG_PKGLIBDIR),
        'runtimedir': Runtimedir(),
    }

    try:
        from . import _offsets as offsets
    except ImportError: # no SGX graphene installed, skipping
        pass
    else:
        env.globals['graphene'].update(
            (k, v) for k, v in offsets.__dict__.items()
            if not k.startswith('_'))

def add_globals_misc(env):
    env.globals['env'] = os.environ
    env.globals['ldd'] = ldd

def make_env():
    env = jinja2.Environment(undefined=jinja2.StrictUndefined, keep_trailing_newline=True)
    add_globals_from_graphene(env)
    add_globals_from_python(env)
    add_globals_misc(env)
    return env

_env = make_env()

def render(template, variables=None):
    '''Render template, given as string. Optional variables may be given as mapping.'''
    return _env.from_string(template).render(**(variables or {}))

def validate_define(_ctx, _param, values):
    ret = {}
    for value in values:
        try:
            k, v = value.split('=', 1)
        except ValueError:
            k, v = value, True
        ret[k] = v
    return ret

@click.command()
@click.option('--string', '-c')
@click.option('--define', '-D', multiple=True, callback=validate_define)
@click.argument('infile', type=click.File('r'), required=False)
@click.argument('outfile', type=click.File('w'), default='-')
def main(string, define, infile, outfile):
    if not bool(string) ^ bool(infile):
        click.get_current_context().fail('specify exactly one of (infile, -c)')
    template = infile.read() if infile else string
    outfile.write(render(template, define))

if __name__ == '__main__':
    main() # pylint: disable=no-value-for-parameter
