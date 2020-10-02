import contextlib
import os
import pathlib
import signal
import subprocess
import unittest

HAS_SGX = os.environ.get('SGX') == '1'
ON_X86 = os.uname().machine in ['x86_64']

def expectedFailureIf(predicate):
    if predicate:
        return unittest.expectedFailure
    return lambda func: func

class RegressionTestCase(unittest.TestCase):
    LOADER_ENV = 'PAL_LOADER'
    LIBPAL_PATH_ENV = 'LIBPAL_PATH'
    HOST_PAL_PATH_ENV = 'HOST_PAL_PATH'
    DEFAULT_TIMEOUT = (20 if HAS_SGX else 10)

    def get_manifest(self, filename):
        return filename + '.manifest' + ('.sgx' if HAS_SGX else '')

    def has_debug(self):
        try:
            libpal = os.environ[self.LIBPAL_PATH_ENV]
        except KeyError:
            self.fail('environment variable {} unset'.format(self.LIBPAL_PATH_ENV))

        p = subprocess.run(['objdump', '-x', libpal], check=True, stdout=subprocess.PIPE)
        dump = p.stdout.decode()
        return '.debug_info' in dump

    def run_gdb(self, args, gdb_script, **kwds):
        try:
            host_pal_path = os.environ[self.HOST_PAL_PATH_ENV]
        except KeyError:
            self.fail('environment variable {} unset'.format(self.HOST_PAL_PATH_ENV))

        # See also pal_loader.
        prefix = ['gdb', '-q']
        env = os.environ.copy()
        if HAS_SGX:
            prefix += ['-x', os.path.join(host_pal_path, 'gdb_integration/graphene_sgx_gdb.py')]
            env['LD_PRELOAD'] = os.path.join(host_pal_path, 'gdb_integration/sgx_gdb.so')
        else:
            prefix += ['-x', os.path.join(host_pal_path, 'gdb_integration/graphene_gdb.py')]

        # Override TTY, as apparently os.setpgrp() confuses GDB and causes it to hang.
        prefix += ['-x', gdb_script, '-batch', '-tty=/dev/null']
        prefix += ['--args']

        return self.run_binary(args, prefix=prefix, env=env, **kwds)

    def run_binary(self, args, *, timeout=None, prefix=None, **kwds):
        timeout = (max(self.DEFAULT_TIMEOUT, timeout) if timeout is not None
            else self.DEFAULT_TIMEOUT)

        try:
            loader = os.environ[self.LOADER_ENV]
        except KeyError:
            self.skipTest(
                'environment variable {} unset'.format(self.LOADER_ENV))

        if not pathlib.Path(loader).exists():
            self.skipTest('loader ({}) not found'.format(loader))

        try:
            libpal = os.environ[self.LIBPAL_PATH_ENV]
        except KeyError:
            self.fail('environment variable {} unset'.format(self.LIBPAL_PATH_ENV))

        if not pathlib.Path(libpal).exists():
            self.skipTest('libpal ({}) not found'.format(libpal))

        if prefix is None:
            prefix = []

        with subprocess.Popen([*prefix, loader, libpal, 'init', *args],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                preexec_fn=os.setpgrp,
                **kwds) as process:
            try:
                stdout, stderr = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                self.fail('timeout ({} s) expired'.format(timeout))

            if process.returncode:
                raise subprocess.CalledProcessError(
                    process.returncode, args, stdout, stderr)

        return stdout.decode(), stderr.decode()

    def run_native_binary(self, args, timeout=None, libpath=None, **kwds):
        timeout = (max(self.DEFAULT_TIMEOUT, timeout) if timeout is not None
            else self.DEFAULT_TIMEOUT)

        my_env = os.environ.copy()
        if not libpath is None:
            my_env["LD_LIBRARY_PATH"] = libpath

        with subprocess.Popen(args,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                env=my_env,
                preexec_fn=os.setpgrp,
                **kwds) as process:
            try:
                stdout, stderr = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                self.fail('timeout ({} s) expired'.format(timeout))

            if process.returncode:
                raise subprocess.CalledProcessError(
                    process.returncode, args, stdout, stderr)

        return stdout.decode(), stderr.decode()

    @contextlib.contextmanager
    def expect_returncode(self, returncode):
        if returncode == 0:
            raise ValueError('expected returncode should be nonzero')
        try:
            yield
            self.fail('did not fail (expected {})'.format(returncode))
        except subprocess.CalledProcessError as e:
            self.assertEqual(e.returncode, returncode,
                'failed with returncode {} (expected {})'.format(
                    e.returncode, returncode))
