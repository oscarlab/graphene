include ../../../../Scripts/Makefile.configs

c_executables-x86_64 = \
	attestation \
	cpuid \
	rdtsc \
	sighandler_divbyzero

ifneq ($(IS_UBSAN_ENABLED),1)
c_executables-x86_64 += \
	debug_regs-x86_64
endif

c_executables = \
	abort \
	abort_multithread \
	bootstrap \
	bootstrap_pie \
	dev \
	device \
	double_fork \
	epoll_wait_timeout \
	eventfd \
	exec \
	exec_fork \
	exec_invalid_args \
	exec_same \
	exec_victim \
	exit \
	exit_group \
	fdleak \
	file_check_policy \
	file_size \
	fopen_cornercases \
	fork_and_exec \
	fp_multithread \
	fstat_cwd \
	futex_bitset \
	futex_requeue \
	futex_timeout \
	futex_wake_op \
	getcwd \
	getdents \
	getsockname \
	getsockopt \
	gettimeofday \
	groups \
	helloworld \
	host_root_fs \
	init_fail \
	kill_all \
	large_mmap \
	large_dir_read \
	madvise \
	mkfifo \
	mmap_file \
	mprotect_file_fork \
	mprotect_prot_growsdown \
	multi_pthread \
	openmp \
	pipe \
	pipe_nonblocking \
	pipe_ocloexec \
	poll \
	poll_closed_fd \
	poll_many_types \
	ppoll \
	proc_common \
	proc_cpuinfo \
	proc_path \
	pselect \
	pthread_set_get_affinity \
	readdir \
	sched \
	sched_set_get_affinity \
	select \
	shared_object \
	sigaction_per_process \
	sigaltstack \
	sighandler_reset \
	sighandler_sigpipe \
	signal_multithread \
	sigprocmask_pending \
	spinlock \
	stat_invalid_args \
	str_close_leak \
	syscall \
	sysfs_common \
	tcp_ipv6_v6only \
	tcp_msg_peek \
	udp \
	unix \
	vfork_and_exec \
	$(c_executables-$(ARCH))

ifneq ($(IS_UBSAN_ENABLED),1)
c_executables += \
	bootstrap_static \
	debug

cxx_executables = bootstrap_cpp
endif

repo_manifests = \
	argv_from_file.manifest \
	debug_log_file.manifest \
	debug_log_inline.manifest \
	env_from_file.manifest \
	env_from_host.manifest \
	file_check_policy_allow_all_but_log.manifest \
	file_check_policy_strict.manifest \
	host_root_fs.manifest \
	init_fail.manifest \
	init_fail2.manifest \
	large_mmap.manifest \
	multi_pthread.manifest \
	multi_pthread_exitless.manifest

# Empty rule to prevent overriding them using "%.manifest: manifest.template" rule.
$(repo_manifests): ;

gen_manifests = $(filter-out $(repo_manifests),$(addsuffix .manifest,$(c_executables) $(cxx_executables)))
all_manifests = $(repo_manifests) $(gen_manifests)

exec_target = \
	$(c_executables) \
	$(cxx_executables)

# repo_manifests needs to be included here to generate .manifest.sgx on SGX also for them.
target = \
	$(exec_target) \
	$(all_manifests)

clean-extra += clean-tmp

extra_rules = \
	-e 's:\$$(RA_CLIENT_SPID):$(if $(RA_CLIENT_SPID),$(RA_CLIENT_SPID),):g' \
	-e 's:\$$(RA_CLIENT_LINKABLE):$(if $(RA_CLIENT_LINKABLE),$(RA_CLIENT_LINKABLE),0):g'

include ../../../../Scripts/manifest.mk
include ../../../../Scripts/Makefile.Test

CFLAGS-bootstrap_static = -static
CFLAGS-bootstrap_pie = -fPIC -pie
CFLAGS-debug = -g3
CFLAGS-debug_regs-x86_64 = -g3
CFLAGS-exec_same = -pthread
CFLAGS-shared_object = -fPIC -pie
CFLAGS-syscall += -I$(PALDIR)/../include -I$(PALDIR)/host/$(PAL_HOST) -I$(PALDIR)/../include/arch/$(ARCH)/Linux
CFLAGS-openmp = -fopenmp
CFLAGS-multi_pthread = -pthread
CFLAGS-exit_group = -pthread
CFLAGS-abort_multithread = -pthread
CFLAGS-eventfd = -pthread
CFLAGS-futex_bitset = -pthread
CFLAGS-futex_requeue = -pthread
CFLAGS-futex_wake_op = -pthread
CFLAGS-proc_common = -pthread
CFLAGS-spinlock += -I$(PALDIR)/../include/lib -I$(PALDIR)/../include/arch/$(ARCH) -pthread
CFLAGS-sigaction_per_process += -pthread
CFLAGS-signal_multithread += -pthread
CFLAGS-pthread_set_get_affinity += -pthread
CFLAGS-gettimeofday += -pthread

CFLAGS-attestation += -I$(PALDIR)/../lib/crypto/mbedtls/include \
                      -I$(PALDIR)/host/Linux-SGX \
                      -I$(PALDIR)/../include/pal
LDLIBS-attestation += $(PALDIR)/../lib/crypto/mbedtls/install/lib/libmbedcrypto.a

CFLAGS-fp_multithread += -pthread -fno-builtin  # see comment in the test's source
LDLIBS-fp_multithread += -lm

%: %.c
	$(call cmd,csingle)

%: %.cpp
	$(call cmd,cxxsingle)

.PHONY: regression
regression: $(target)
	$(RM) libos-regression.xml
	$(MAKE) libos-regression.xml

libos-regression.xml: test_libos.py $(call expand_target_to_token,$(target))
	../../../../Scripts/run-pytest --junit-xml $@ -v $<

.PHONY: clean-tmp
clean-tmp:
	$(RM) -r *.tmp *.cached *.manifest.sgx *~ *.sig *.token .cache __pycache__ libos-regression.xml testfile tmp/*
