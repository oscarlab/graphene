#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static long inline_stat(const char* filename, struct stat* statbuf) {
    return syscall(SYS_stat, filename, statbuf);
}

static long inline_lstat(const char* filename, struct stat* statbuf) {
    return syscall(SYS_lstat, filename, statbuf);
}

int main(int argc, char** argv) {
    int r;
    struct stat buf;

    char* goodpath = argv[0];
    char* badpath  = (void*)-1;

    struct stat* goodbuf = &buf;
    struct stat* badbuf  = (void*)-1;

    /* check stat() */
    r = inline_stat(badpath, goodbuf);
    if (r == -1 && errno == EFAULT)
        puts("stat(invalid-path-ptr) correctly returned error");

    r = inline_stat(goodpath, badbuf);
    if (r == -1 && errno == EFAULT)
        puts("stat(invalid-buf-ptr) correctly returned error");

    /* check lstat() */
    r = inline_lstat(badpath, goodbuf);
    if (r == -1 && errno == EFAULT)
        puts("lstat(invalid-path-ptr) correctly returned error");

    r = inline_lstat(goodpath, badbuf);
    if (r == -1 && errno == EFAULT)
        puts("lstat(invalid-buf-ptr) correctly returned error");

    return 0;
}
