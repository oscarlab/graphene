#!/usr/bin/env python3

import os
import re
import shutil
import signal
import subprocess
import unittest

from regression import (
    HAS_SGX,
    ON_X86,
    USES_MUSL,
    RegressionTestCase,
)

class TC_00_Unittests(RegressionTestCase):
    def test_000_spinlock(self):
        stdout, _ = self.run_binary(['spinlock'], timeout=20)

        self.assertIn('Test successful!', stdout)

class TC_01_Bootstrap(RegressionTestCase):
    def test_001_helloworld(self):
        stdout, _ = self.run_binary(['helloworld'])
        self.assertIn('Hello world!', stdout)

    def test_100_basic_bootstrapping(self):
        stdout, _ = self.run_binary(['bootstrap'])

        # Basic Bootstrapping
        self.assertIn('User Program Started', stdout)

        # One Argument Given
        self.assertIn('# of arguments: 1', stdout)
        self.assertIn('argv[0] = bootstrap', stdout)

    def test_101_basic_bootstrapping_five_arguments(self):
        # Five Arguments Given
        stdout, _ = self.run_binary(['bootstrap', 'a', 'b', 'c', 'd'])
        self.assertIn('# of arguments: 5', stdout)
        self.assertIn('argv[0] = bootstrap', stdout)
        self.assertIn('argv[1] = a', stdout)
        self.assertIn('argv[2] = b', stdout)
        self.assertIn('argv[3] = c', stdout)
        self.assertIn('argv[4] = d', stdout)

    def test_102_argv_from_file(self):
        args = ['bootstrap', 'THIS', 'SHOULD GO', 'TO', '\nTHE\n', 'APP']
        result = subprocess.run(['../../../../Tools/argv_serializer'] + args,
                                stdout=subprocess.PIPE, check=True)
        with open('argv_test_input', 'wb') as f:
            f.write(result.stdout)
        try:
            stdout, _ = self.run_binary(['argv_from_file', 'WRONG', 'ARGUMENTS'])
            self.assertIn('# of arguments: %d\n' % len(args), stdout)
            for i, arg in enumerate(args):
                self.assertIn('argv[%d] = %s\n' % (i, arg), stdout)
        finally:
            os.remove('argv_test_input')

    def test_103_env_from_host(self):
        host_envs = {
            'A': '123',
            'PWD': '/some_dir',
            'some weir:d\nvar_name': ' even we\nirder\tvalue',
        }
        manifest_envs = {'LD_LIBRARY_PATH': '/lib'}
        stdout, _ = self.run_binary(['env_from_host'], env=host_envs)
        self.assertIn('# of envs: %d\n' % (len(host_envs) + len(manifest_envs)), stdout)
        for _, (key, val) in enumerate({**host_envs, **manifest_envs}.items()):
            # We don't enforce any specific order of envs, so we skip checking the index.
            self.assertIn('] = %s\n' % (key + '=' + val), stdout)

    def test_104_env_from_file(self):
        envs = ['A=123', 'PWD=/some_dir', 'some weir:d\nvar_name= even we\nirder\tvalue']
        manifest_envs = ['LD_LIBRARY_PATH=/lib']
        host_envs = {'THIS_SHOULDNT_BE_PASSED': '1234'}
        result = subprocess.run(['../../../../Tools/argv_serializer'] + envs,
                                stdout=subprocess.PIPE, check=True)
        with open('env_test_input', 'wb') as f:
            f.write(result.stdout)
        try:
            stdout, _ = self.run_binary(['env_from_file'], env=host_envs)
            self.assertIn('# of envs: %d\n' % (len(envs) + len(manifest_envs)), stdout)
            for _, arg in enumerate(envs + manifest_envs):
                # We don't enforce any specific order of envs, so we skip checking the index.
                self.assertIn('] = %s\n' % arg, stdout)
        finally:
            os.remove('env_test_input')

    @unittest.skipUnless(HAS_SGX,
        'This test is only meaningful on SGX PAL because only SGX catches raw '
        'syscalls and redirects to Graphene\'s LibOS. If we will add seccomp to '
        'Linux PAL, then we should allow this test on Linux PAL as well.')
    def test_105_basic_bootstrapping_static(self):
        stdout, _ = self.run_binary(['bootstrap_static'])
        self.assertIn('Hello world (bootstrap_static)!', stdout)

    def test_106_basic_bootstrapping_pie(self):
        stdout, _ = self.run_binary(['bootstrap_pie'])
        self.assertIn('User program started', stdout)
        self.assertIn('Local Address in Executable: 0x', stdout)
        self.assertIn('argv[0] = bootstrap_pie', stdout)

    @unittest.skipIf(USES_MUSL, 'musl doesn\'t contain libstdc++.so')
    def test_110_basic_bootstrapping_cpp(self):
        stdout, _ = self.run_binary(['bootstrap_cpp'])
        self.assertIn('User Program Started', stdout)

    def test_200_exec(self):
        stdout, _ = self.run_binary(['exec'])

        # 2 page child binary
        self.assertIn(
            '0' * 89 + ' ' +
            ('0' * 93 + ' ') * 15,
            stdout)

    def test_201_exec_same(self):
        args = ['arg_#%d' % i for i in range(50)]
        stdout, _ = self.run_binary(['exec_same'] + args, timeout=40)
        for arg in args:
            self.assertIn(arg + '\n', stdout)

    def test_202_fork_and_exec(self):
        stdout, _ = self.run_binary(['fork_and_exec'], timeout=60)

        # fork and exec 2 page child binary
        self.assertIn('child exited with status: 0', stdout)
        self.assertIn('test completed successfully', stdout)

    def test_203_vfork_and_exec(self):
        stdout, _ = self.run_binary(['vfork_and_exec'], timeout=60)

        # vfork and exec 2 page child binary
        self.assertIn('child exited with status: 0', stdout)
        self.assertIn('test completed successfully', stdout)

    def test_204_exec_fork(self):
        stdout, _ = self.run_binary(['exec_fork'], timeout=60)
        self.assertNotIn('Handled SIGCHLD', stdout)
        self.assertIn('Set up handler for SIGCHLD', stdout)
        self.assertIn('child exited with status: 0', stdout)
        self.assertIn('test completed successfully', stdout)

    def test_205_double_fork(self):
        stdout, stderr = self.run_binary(['double_fork'])
        self.assertIn('TEST OK', stdout)
        self.assertNotIn('grandchild', stderr)

    def test_210_exec_invalid_args(self):
        stdout, _ = self.run_binary(['exec_invalid_args'])

        # Execve with invalid pointers in arguments
        self.assertIn('execve(invalid-path) correctly returned error', stdout)
        self.assertIn('execve(invalid-argv-ptr) correctly returned error', stdout)
        self.assertIn('execve(invalid-envp-ptr) correctly returned error', stdout)
        self.assertIn('execve(invalid-argv) correctly returned error', stdout)
        self.assertIn('execve(invalid-envp) correctly returned error', stdout)

    def test_300_shared_object(self):
        stdout, _ = self.run_binary(['shared_object'])

        # Shared Object
        self.assertIn('Hello world', stdout)

    def test_400_exit(self):
        with self.expect_returncode(113):
            self.run_binary(['exit'])

    def test_401_exit_group(self):
        for thread_idx in range(4):
            exit_code = 100 + thread_idx
            try:
                self.run_binary(['exit_group', str(thread_idx), str(exit_code)])
                self.fail('exit_group returned 0 instead of {}'.format(exit_code))
            except subprocess.CalledProcessError as e:
                self.assertEqual(e.returncode, exit_code)

    def test_402_signalexit(self):
        with self.expect_returncode(134):
            self.run_binary(['abort'])

    def test_403_signalexit_multithread(self):
        with self.expect_returncode(134):
            self.run_binary(['abort_multithread'])

    def test_404_sigprocmask_pending(self):
        stdout, _ = self.run_binary(['sigprocmask_pending'], timeout=60)
        self.assertIn('Child OK', stdout)
        self.assertIn('All tests OK', stdout)

    def test_500_init_fail(self):
        try:
            self.run_binary(['init_fail'])
            self.fail('expected to return nonzero (and != 42)')
        except subprocess.CalledProcessError as e:
            self.assertNotEqual(e.returncode, 42, 'expected returncode != 42')

    @unittest.skipUnless(HAS_SGX, 'This test relies on SGX-specific manifest options.')
    def test_501_init_fail2(self):
        try:
            self.run_binary(['init_fail2'], timeout=60)
            self.fail('expected to return nonzero (and != 42)')
        except subprocess.CalledProcessError as e:
            self.assertNotEqual(e.returncode, 42, 'expected returncode != 42')

    def test_600_multi_pthread(self):
        stdout, _ = self.run_binary(['multi_pthread'])

        # Multiple thread creation
        self.assertIn('128 Threads Created', stdout)

    @unittest.skipUnless(HAS_SGX, 'This test is only meaningful on SGX PAL')
    def test_601_multi_pthread_exitless(self):
        stdout, _ = self.run_binary(['multi_pthread_exitless'], timeout=60)

        # Multiple thread creation
        self.assertIn('128 Threads Created', stdout)

    def test_602_fp_multithread(self):
        stdout, _ = self.run_binary(['fp_multithread'])
        self.assertIn('FE_TONEAREST   child: 42.5 = 42.0, -42.5 = -42.0', stdout)
        self.assertIn('FE_TONEAREST  parent: 42.5 = 42.0, -42.5 = -42.0', stdout)
        self.assertIn('FE_UPWARD      child: 42.5 = 43.0, -42.5 = -42.0', stdout)
        self.assertIn('FE_UPWARD     parent: 42.5 = 43.0, -42.5 = -42.0', stdout)
        self.assertIn('FE_DOWNWARD    child: 42.5 = 42.0, -42.5 = -43.0', stdout)
        self.assertIn('FE_DOWNWARD   parent: 42.5 = 42.0, -42.5 = -43.0', stdout)
        self.assertIn('FE_TOWARDZERO  child: 42.5 = 42.0, -42.5 = -42.0', stdout)
        self.assertIn('FE_TOWARDZERO parent: 42.5 = 42.0, -42.5 = -42.0', stdout)

    def test_700_debug_log_inline(self):
        _, stderr = self.run_binary(['debug_log_inline'])
        self._verify_debug_log(stderr)

    def test_701_debug_log_file(self):
        log_path = 'tmp/debug_log_file.log'
        if os.path.exists(log_path):
            os.remove(log_path)

        self.run_binary(['debug_log_file'])

        with open(log_path) as log_file:
            log = log_file.read()

        self._verify_debug_log(log)

    def _verify_debug_log(self, log: str):
        self.assertIn('Host:', log)
        self.assertIn('Shim process initialized', log)
        self.assertIn('--- shim_exit_group', log)


@unittest.skipUnless(HAS_SGX,
    'This test is only meaningful on SGX PAL because only SGX catches raw '
    'syscalls and redirects to Graphene\'s LibOS. If we will add seccomp to '
    'Linux PAL, then we should allow this test on Linux PAL as well.')
@unittest.skipIf(USES_MUSL, 'musl seems to be incompatible with Ubuntu\'s libgomp.so')
class TC_02_OpenMP(RegressionTestCase):
    def test_000_simple_for_loop(self):
        stdout, _ = self.run_binary(['openmp'])

        # OpenMP simple for loop
        self.assertIn('first: 0, last: 9', stdout)

@unittest.skipUnless(HAS_SGX,
    'This test is only meaningful on SGX PAL because file-check-policy is '
    'only relevant to SGX.')
class TC_03_FileCheckPolicy(RegressionTestCase):
    def test_000_strict_success(self):
        stdout, _ = self.run_binary(['file_check_policy_strict', 'trusted_testfile'])

        self.assertIn('file_check_policy succeeded', stdout)

    def test_001_strict_fail(self):
        with self.expect_returncode(2):
            self.run_binary(['file_check_policy_strict', 'unknown_testfile'])

    def test_002_allow_all_but_log_success(self):
        stdout, stderr = self.run_binary(['file_check_policy_allow_all_but_log',
                                          'unknown_testfile'])

        self.assertIn('Allowing access to an unknown file due to file_check_policy settings: '
                      'file:unknown_testfile', stderr)
        self.assertIn('file_check_policy succeeded', stdout)

    def test_003_allow_all_but_log_fail(self):
        stdout, stderr = self.run_binary(['file_check_policy_allow_all_but_log',
                                          'trusted_testfile'])

        self.assertNotIn('Allowing access to an unknown file due to file_check_policy settings: '
                         'file:trusted_testfile', stderr)
        self.assertIn('file_check_policy succeeded', stdout)

@unittest.skipUnless(HAS_SGX,
    'These tests are only meaningful on SGX PAL because only SGX supports attestation.')
class TC_04_Attestation(RegressionTestCase):
    def test_000_attestation(self):
        stdout, _ = self.run_binary(['attestation'], timeout=60)
        self.assertIn("Test resource leaks in attestation filesystem... SUCCESS", stdout)
        self.assertIn("Test local attestation... SUCCESS", stdout)
        self.assertIn("Test quote interface... SUCCESS", stdout)

    def test_001_attestation_stdio(self):
        stdout, _ = self.run_binary(['attestation', 'test_stdio'], timeout=60)
        self.assertIn("Test resource leaks in attestation filesystem... SUCCESS", stdout)
        self.assertIn("Test local attestation... SUCCESS", stdout)
        self.assertIn("Test quote interface... SUCCESS", stdout)

class TC_30_Syscall(RegressionTestCase):
    def test_000_getcwd(self):
        stdout, _ = self.run_binary(['getcwd'])

        # Getcwd syscall
        self.assertIn('[bss_cwd_buf] getcwd succeeded: /', stdout)
        self.assertIn('[mmapped_cwd_buf] getcwd succeeded: /', stdout)

    def test_010_stat_invalid_args(self):
        stdout, _ = self.run_binary(['stat_invalid_args'])

        # Stat with invalid arguments
        self.assertIn('stat(invalid-path-ptr) correctly returned error', stdout)
        self.assertIn('stat(invalid-buf-ptr) correctly returned error', stdout)
        self.assertIn('lstat(invalid-path-ptr) correctly returned error', stdout)
        self.assertIn('lstat(invalid-buf-ptr) correctly returned error', stdout)

    def test_011_fstat_cwd(self):
        stdout, _ = self.run_binary(['fstat_cwd'])

        # fstat on a directory
        self.assertIn('fstat returned the fd type as S_IFDIR', stdout)

    def test_020_getdents(self):
        if os.path.exists("root"):
            shutil.rmtree("root")

        # This doesn't catch extraneous entries, but should be fine
        # until the LTP test can be run (need symlink support)

        stdout, _ = self.run_binary(['getdents'])
        self.assertIn('getdents: setup ok', stdout)

        # Directory listing (32-bit)
        self.assertIn('getdents32: . [0x4]', stdout)
        self.assertIn('getdents32: .. [0x4]', stdout)
        self.assertIn('getdents32: file1 [0x8]', stdout)
        self.assertIn('getdents32: file2 [0x8]', stdout)
        self.assertIn('getdents32: dir3 [0x4]', stdout)

        # Directory listing (64-bit)
        self.assertIn('getdents64: . [0x4]', stdout)
        self.assertIn('getdents64: .. [0x4]', stdout)
        self.assertIn('getdents64: file1 [0x8]', stdout)
        self.assertIn('getdents64: file2 [0x8]', stdout)
        self.assertIn('getdents64: dir3 [0x4]', stdout)

    def test_021_getdents_large_dir(self):
        if os.path.exists("tmp/large_dir"):
            shutil.rmtree("tmp/large_dir")
        stdout, _ = self.run_binary(['large_dir_read', 'tmp/large_dir', '3000'])

        self.assertIn('Success!', stdout)

    def test_022_host_root_fs(self):
        stdout, _ = self.run_binary(['host_root_fs'])
        self.assertIn('Test was successful', stdout)

    def test_030_fopen(self):
        if os.path.exists("tmp/filecreatedbygraphene"):
            os.remove("tmp/filecreatedbygraphene")
        stdout, _ = self.run_binary(['fopen_cornercases'])

        # fopen corner cases
        self.assertIn('Successfully read from file: Hello World', stdout)

    def test_031_readdir(self):
        stdout, _ = self.run_binary(['readdir'])
        self.assertIn('test completed successfully', stdout)

    def test_032_file_size(self):
        stdout, _ = self.run_binary(['file_size'])
        self.assertIn('test completed successfully', stdout)

    def test_040_futex_bitset(self):
        stdout, _ = self.run_binary(['futex_bitset'])

        # Futex Wake Test
        self.assertIn('Woke all kiddos', stdout)

    def test_041_futex_timeout(self):
        stdout, _ = self.run_binary(['futex_timeout'])

        # Futex Timeout Test
        self.assertIn('futex correctly timed out', stdout)

    def test_042_futex_requeue(self):
        stdout, _ = self.run_binary(['futex_requeue'])

        self.assertIn('Test successful!', stdout)

    def test_043_futex_wake_op(self):
        stdout, _ = self.run_binary(['futex_wake_op'])

        self.assertIn('Test successful!', stdout)

    def test_050_mmap(self):
        stdout, _ = self.run_binary(['mmap_file'], timeout=60)

        # Private mmap beyond file range
        self.assertIn('mmap test 6 passed', stdout)
        self.assertIn('mmap test 7 passed', stdout)

        # Private mmap beyond file range (after fork)
        self.assertIn('mmap test 1 passed', stdout)
        self.assertIn('mmap test 2 passed', stdout)
        self.assertIn('mmap test 3 passed', stdout)
        self.assertIn('mmap test 4 passed', stdout)

        # "test 5" and "test 8" are checked below, in test_051_mmap_sgx

    @unittest.skipIf(HAS_SGX,
        'On SGX, SIGBUS isn\'t always implemented correctly, for lack '
        'of memory protection. For now, some of these cases won\'t work.')
    def test_051_mmap_sgx(self):
        stdout, _ = self.run_binary(['mmap_file'], timeout=60)

        # SIGBUS test
        self.assertIn('mmap test 5 passed', stdout)
        self.assertIn('mmap test 8 passed', stdout)

    def test_052_large_mmap(self):
        try:
            stdout, _ = self.run_binary(['large_mmap'], timeout=480)

            # Ftruncate
            self.assertIn('large_mmap: ftruncate OK', stdout)

            # Large mmap
            self.assertIn('large_mmap: mmap 1 completed OK', stdout)
            self.assertIn('large_mmap: mmap 2 completed OK', stdout)
        finally:
            # This test generates a 4 GB file, don't leave it in FS.
            os.remove('testfile')

    def test_053_mprotect_file_fork(self):
        stdout, _ = self.run_binary(['mprotect_file_fork'])

        self.assertIn('Test successful!', stdout)

    def test_054_mprotect_prot_growsdown(self):
        stdout, _ = self.run_binary(['mprotect_prot_growsdown'])

        self.assertIn('TEST OK', stdout)

    def test_055_madvise(self):
        stdout, _ = self.run_binary(['madvise'])
        self.assertIn('TEST OK', stdout)

    @unittest.skip('sigaltstack isn\'t correctly implemented')
    def test_060_sigaltstack(self):
        stdout, _ = self.run_binary(['sigaltstack'])

        # Sigaltstack Test
        self.assertIn('OK on sigaltstack in main thread before alarm', stdout)
        self.assertIn('&act == 0x', stdout)
        self.assertIn('sig %d count 1 goes off with sp=0x' % signal.SIGALRM, stdout)
        self.assertIn('OK on signal stack', stdout)
        self.assertIn('OK on sigaltstack in handler', stdout)
        self.assertIn('sig %d count 2 goes off with sp=0x' % signal.SIGALRM, stdout)
        self.assertIn('OK on signal stack', stdout)
        self.assertIn('OK on sigaltstack in handler', stdout)
        self.assertIn('sig %d count 3 goes off with sp=0x' % signal.SIGALRM, stdout)
        self.assertIn('OK on signal stack', stdout)
        self.assertIn('OK on sigaltstack in handler', stdout)
        self.assertIn('OK on sigaltstack in main thread', stdout)
        self.assertIn('done exiting', stdout)

    def test_070_eventfd(self):
        stdout, _ = self.run_binary(['eventfd'])

        # Eventfd Test
        self.assertIn('eventfd_using_poll completed successfully', stdout)
        self.assertIn('eventfd_using_various_flags completed successfully', stdout)
        self.assertIn('eventfd_using_fork completed successfully', stdout)

    @unittest.skipIf(USES_MUSL, 'musl doesn\'t implement scheduling APIs')
    def test_080_sched(self):
        stdout, _ = self.run_binary(['sched'])

        # Scheduling Syscalls Test
        self.assertIn('Test completed successfully', stdout)

    def test_090_sighandler_reset(self):
        stdout, _ = self.run_binary(['sighandler_reset'])
        self.assertIn('Got signal %d' % signal.SIGCHLD, stdout)
        self.assertIn('Handler was invoked 1 time(s).', stdout)

    def test_091_sigaction_per_process(self):
        stdout, _ = self.run_binary(['sigaction_per_process'])
        self.assertIn('TEST OK', stdout)

    def test_092_sighandler_sigpipe(self):
        try:
            self.run_binary(['sighandler_sigpipe'])
            self.fail('expected to return nonzero')
        except subprocess.CalledProcessError as e:
            # FIXME: It's unclear what Graphene process should return when the app
            # inside dies due to a signal.
            self.assertTrue(e.returncode in [signal.SIGPIPE, 128 + signal.SIGPIPE])
            stdout = e.stdout.decode()
            self.assertIn('Got signal %d' % signal.SIGPIPE, stdout)
            self.assertIn('Got 1 SIGPIPE signal(s)', stdout)
            self.assertIn('Could not write to pipe: Broken pipe', stdout)

    @unittest.skipUnless(ON_X86, "x86-specific")
    def test_093_sighandler_divbyzero(self):
        stdout, _ = self.run_binary(['sighandler_divbyzero'])
        self.assertIn('Got signal %d' % signal.SIGFPE, stdout)
        self.assertIn('Got 1 SIGFPE signal(s)', stdout)
        self.assertIn('TEST OK', stdout)

    def test_094_signal_multithread(self):
        stdout, _ = self.run_binary(['signal_multithread'])
        self.assertIn('TEST OK', stdout)

    def test_095_kill_all(self):
        stdout, _ = self.run_binary(['kill_all'])
        self.assertIn('TEST OK', stdout)

    def test_100_get_set_groups(self):
        stdout, _ = self.run_binary(['groups'])
        self.assertIn('child OK', stdout)
        self.assertIn('parent OK', stdout)

    def test_101_sched_set_get_cpuaffinity(self):
        stdout, _ = self.run_binary(['sched_set_get_affinity'])
        self.assertIn('TEST OK', stdout)

    def test_102_pthread_set_get_affinity(self):
        stdout, _ = self.run_binary(['pthread_set_get_affinity', '1000'])
        self.assertIn('TEST OK', stdout)

    def test_103_gettimeofday(self):
        stdout, _ = self.run_binary(['gettimeofday'])
        self.assertIn('TEST OK', stdout)

@unittest.skipUnless(HAS_SGX,
    'This test is only meaningful on SGX PAL because only SGX catches raw '
    'syscalls and redirects to Graphene\'s LibOS. If we will add seccomp to '
    'Linux PAL, then we should allow this test on Linux PAL as well.')
class TC_31_SyscallSGX(RegressionTestCase):
    def test_000_syscall_redirect(self):
        stdout, _ = self.run_binary(['syscall'])

        # Syscall Instruction Redirection
        self.assertIn('Hello world', stdout)

class TC_40_FileSystem(RegressionTestCase):
    def test_000_proc(self):
        (DT_DIR, DT_REG) = (4, 8,)
        stdout, _ = self.run_binary(['proc_common'])
        self.assertIn('/proc/1/..', stdout)
        self.assertIn('/proc/1/cwd', stdout)
        self.assertIn('/proc/1/exe', stdout)
        self.assertIn('/proc/1/root', stdout)
        self.assertIn('/proc/1/fd', stdout)
        self.assertIn('/proc/1/maps', stdout)
        self.assertIn('/proc/self/..', stdout)
        self.assertIn('/proc/self/cwd', stdout)
        self.assertIn('/proc/self/exe', stdout)
        self.assertIn('/proc/self/root', stdout)
        self.assertIn('/proc/self/fd', stdout)
        self.assertIn('/proc/self/maps', stdout)
        self.assertIn('/proc/., type: {0}'.format(DT_DIR), stdout)
        self.assertIn('/proc/1, type: {0}'.format(DT_DIR), stdout)
        self.assertIn('/proc/2, type: {0}'.format(DT_DIR), stdout)
        self.assertIn('/proc/3, type: {0}'.format(DT_DIR), stdout)
        self.assertIn('/proc/4, type: {0}'.format(DT_DIR), stdout)
        self.assertIn('/proc/self, type: {0}'.format(DT_DIR), stdout)
        self.assertIn('/proc/meminfo, type: {0}'.format(DT_REG), stdout)
        self.assertIn('/proc/cpuinfo, type: {0}'.format(DT_REG), stdout)
        self.assertIn('symlink /proc/self/exec resolves to /proc_common', stdout)
        self.assertIn('/proc/2/cwd/proc_common.c', stdout)
        self.assertIn(' 00:00 0 /proc_common', stdout) # from /proc/<pid>/maps
        self.assertIn('stack', stdout)
        self.assertIn('vendor_id', stdout)

    def test_001_dev(self):
        stdout, _ = self.run_binary(['dev'])
        self.assertIn('/dev/.', stdout)
        self.assertIn('/dev/null', stdout)
        self.assertIn('/dev/zero', stdout)
        self.assertIn('/dev/random', stdout)
        self.assertIn('/dev/urandom', stdout)
        self.assertIn('/dev/stdin', stdout)
        self.assertIn('/dev/stdout', stdout)
        self.assertIn('/dev/stderr', stdout)
        self.assertIn('Four bytes from /dev/urandom', stdout)
        self.assertIn('TEST OK', stdout)

    def test_002_device(self):
        stdout, _ = self.run_binary(['device'])
        self.assertIn('TEST OK', stdout)

    def test_010_path(self):
        stdout, _ = self.run_binary(['proc_path'])
        self.assertIn('proc path test success', stdout)

    def test_020_cpuinfo(self):
        stdout, _ = self.run_binary(['proc_cpuinfo'], timeout=50)

        # proc/cpuinfo Linux-based formatting
        self.assertIn('cpuinfo test passed', stdout)

    def test_030_fdleak(self):
        stdout, _ = self.run_binary(['fdleak'], timeout=10)
        self.assertIn("Test succeeded.", stdout)

    def test_040_str_close_leak(self):
        stdout, _ = self.run_binary(['str_close_leak'], timeout=60)
        self.assertIn("Success", stdout)

    @unittest.skipIf(USES_MUSL, 'musl is missing gnu_dev_minor and gnu_dev_major symbols if the '
                                'binaries weren\'t built against musl headers')
    def test_050_sysfs(self):
        stdout, _ = self.run_binary(['sysfs_common'])
        self.assertIn('TEST OK', stdout)


class TC_50_GDB(RegressionTestCase):
    def setUp(self):
        if not self.has_debug():
            self.skipTest('test runs only when Graphene is compiled with DEBUG=1')

    def find(self, name, stdout):
        match = re.search('<{0} start>(.*)<{0} end>'.format(name), stdout, re.DOTALL)
        self.assertTrue(match, '{} not found in GDB output'.format(name))
        return match.group(1).strip()

    def test_000_gdb_backtrace(self):
        # pylint: disable=fixme
        #
        # To run this test manually, use:
        # GDB=1 GDB_SCRIPT=debug.gdb ./pal_loader debug
        #
        # TODO: strengthen this test after SGX includes enclave entry.
        #
        # While the stack trace in SGX is unbroken, it currently starts at _start inside
        # enclave, instead of including eclave entry.

        stdout, _ = self.run_gdb(['debug'], 'debug.gdb')

        backtrace_1 = self.find('backtrace 1', stdout)
        self.assertIn(' main () at debug.c', backtrace_1)
        self.assertIn(' _start ()', backtrace_1)
        self.assertNotIn('??', backtrace_1)

        backtrace_2 = self.find('backtrace 2', stdout)
        self.assertIn(' dev_write (', backtrace_2)
        self.assertIn(' func () at debug.c', backtrace_2)
        self.assertIn(' main () at debug.c', backtrace_2)
        self.assertIn(' _start ()', backtrace_2)
        self.assertNotIn('??', backtrace_2)

        if HAS_SGX:
            backtrace_3 = self.find('backtrace 3', stdout)
            self.assertIn(' sgx_ocall_write (', backtrace_3)
            self.assertIn(' dev_write (', backtrace_3)
            self.assertIn(' func () at debug.c', backtrace_3)
            self.assertIn(' main () at debug.c', backtrace_3)
            self.assertIn(' _start ()', backtrace_3)
            self.assertNotIn('??', backtrace_3)

    @unittest.skipUnless(ON_X86, 'x86-specific')
    def test_010_regs_x86_64(self):
        # To run this test manually, use:
        # GDB=1 GDB_SCRIPT=debug_regs-x86_64.gdb ./pal_loader debug_regs-x86_64

        stdout, _ = self.run_gdb(['debug_regs-x86_64'], 'debug_regs-x86_64.gdb')

        rdx = self.find('RDX', stdout)
        self.assertEqual(rdx, '$1 = 0x1000100010001000')

        rdx_result = self.find('RDX result', stdout)
        self.assertEqual(rdx_result, '$2 = 0x2000200020002000')

        xmm0 = self.find('XMM0', stdout)
        self.assertEqual(xmm0, '$3 = 0x30003000300030003000300030003000')

        xmm0_result = self.find('XMM0 result', stdout)
        self.assertEqual(xmm0_result, '$4 = 0x4000400040004000')


class TC_80_Socket(RegressionTestCase):
    def test_000_getsockopt(self):
        stdout, _ = self.run_binary(['getsockopt'])
        self.assertIn('getsockopt: Got socket type OK', stdout)
        self.assertIn('getsockopt: Got TCP_NODELAY flag OK', stdout)

    def test_010_epoll_wait_timeout(self):
        stdout, _ = self.run_binary(['epoll_wait_timeout', '8000'],
            timeout=50)

        # epoll_wait timeout
        self.assertIn('epoll_wait test passed', stdout)

    def test_020_poll(self):
        stdout, _ = self.run_binary(['poll'])
        self.assertIn('poll(POLLOUT) returned 1 file descriptors', stdout)
        self.assertIn('poll(POLLIN) returned 1 file descriptors', stdout)

    def test_021_poll_many_types(self):
        stdout, _ = self.run_binary(['poll_many_types'])
        self.assertIn('poll(POLLIN) returned 3 file descriptors', stdout)

    def test_022_poll_closed_fd(self):
        stdout, _ = self.run_binary(['poll_closed_fd'], timeout=60)
        self.assertNotIn('poll with POLLIN failed', stdout)
        self.assertIn('read on pipe: Hello from write end of pipe!', stdout)
        self.assertIn('the peer closed its end of the pipe', stdout)

    def test_030_ppoll(self):
        stdout, _ = self.run_binary(['ppoll'])
        self.assertIn('ppoll(POLLOUT) returned 1 file descriptors', stdout)
        self.assertIn('ppoll(POLLIN) returned 1 file descriptors', stdout)

    def test_040_select(self):
        stdout, _ = self.run_binary(['select'])
        self.assertIn('select() on write event returned 1 file descriptors', stdout)
        self.assertIn('select() on read event returned 1 file descriptors', stdout)

    def test_050_pselect(self):
        stdout, _ = self.run_binary(['pselect'])
        self.assertIn('pselect() on write event returned 1 file descriptors', stdout)
        self.assertIn('pselect() on read event returned 1 file descriptors', stdout)

    def test_060_getsockname(self):
        stdout, _ = self.run_binary(['getsockname'])
        self.assertIn('getsockname: Got socket name with static port OK', stdout)
        self.assertIn('getsockname: Got socket name with arbitrary port OK', stdout)

    def test_090_pipe(self):
        stdout, _ = self.run_binary(['pipe'], timeout=60)
        self.assertIn('read on pipe: Hello from write end of pipe!', stdout)

    def test_091_pipe_nonblocking(self):
        stdout, _ = self.run_binary(['pipe_nonblocking'])
        self.assertIn('TEST OK', stdout)

    def test_092_pipe_ocloexec(self):
        stdout, _ = self.run_binary(['pipe_ocloexec'])
        self.assertIn('TEST OK', stdout)

    def test_095_mkfifo(self):
        stdout, _ = self.run_binary(['mkfifo'], timeout=60)
        self.assertIn('read on FIFO: Hello from write end of FIFO!', stdout)

    def test_100_socket_unix(self):
        stdout, _ = self.run_binary(['unix'])
        self.assertIn('Data: This is packet 0', stdout)
        self.assertIn('Data: This is packet 1', stdout)
        self.assertIn('Data: This is packet 2', stdout)
        self.assertIn('Data: This is packet 3', stdout)
        self.assertIn('Data: This is packet 4', stdout)
        self.assertIn('Data: This is packet 5', stdout)
        self.assertIn('Data: This is packet 6', stdout)
        self.assertIn('Data: This is packet 7', stdout)
        self.assertIn('Data: This is packet 8', stdout)
        self.assertIn('Data: This is packet 9', stdout)

    def test_200_socket_udp(self):
        stdout, _ = self.run_binary(['udp'], timeout=50)
        self.assertIn('Data: This is packet 0', stdout)
        self.assertIn('Data: This is packet 1', stdout)
        self.assertIn('Data: This is packet 2', stdout)
        self.assertIn('Data: This is packet 3', stdout)
        self.assertIn('Data: This is packet 4', stdout)
        self.assertIn('Data: This is packet 5', stdout)
        self.assertIn('Data: This is packet 6', stdout)
        self.assertIn('Data: This is packet 7', stdout)
        self.assertIn('Data: This is packet 8', stdout)
        self.assertIn('Data: This is packet 9', stdout)

    def test_300_socket_tcp_msg_peek(self):
        stdout, _ = self.run_binary(['tcp_msg_peek'], timeout=50)
        self.assertIn('[client] receiving with MSG_PEEK: Hello from server!', stdout)
        self.assertIn('[client] receiving with MSG_PEEK again: Hello from server!', stdout)
        self.assertIn('[client] receiving without MSG_PEEK: Hello from server!', stdout)
        self.assertIn('[client] checking how many bytes are left unread: 0', stdout)
        self.assertIn('[client] done', stdout)
        self.assertIn('[server] done', stdout)

    def test_310_socket_tcp_ipv6_v6only(self):
        stdout, _ = self.run_binary(['tcp_ipv6_v6only'], timeout=50)
        self.assertIn('test completed successfully', stdout)

@unittest.skipUnless(HAS_SGX,
    'This test is only meaningful on SGX PAL because only SGX emulates CPUID.')
class TC_90_CpuidSGX(RegressionTestCase):
    def test_000_cpuid(self):
        stdout, _ = self.run_binary(['cpuid'])
        self.assertIn('CPUID test passed.', stdout)

# note that `rdtsc` also correctly runs on non-SGX PAL, but non-SGX CPU may not have rdtscp
@unittest.skipUnless(HAS_SGX,
    'This test is only meaningful on SGX PAL because only SGX emulates RDTSC/RDTSCP.')
class TC_91_RdtscSGX(RegressionTestCase):
    def test_000_rdtsc(self):
        stdout, _ = self.run_binary(['rdtsc'])
        self.assertIn('TEST OK', stdout)
