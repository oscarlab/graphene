#include <stddef.h>

int ecall_enclave_start(char* libpal_uri, char* args, size_t args_size, char* env, size_t env_size);

int ecall_thread_start(void);

int ecall_thread_reset(void);
