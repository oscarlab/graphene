#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MSG "Hello from /proc/1/fd/1\n"
#define MSG2 "Hello from /dev/stdout\n"

static void* fn(void* arg) {
    /* not to consume CPU, each thread simply sleeps */
    sleep(10000);
    return NULL;
}

int main(int argc, char** argv) {
    pthread_t thread[3];
    int ret;
    FILE* f;
    DIR* dir;
    struct dirent* dirent;
    char buf[4096];

    /* create three threads so we have some info in /proc/[pid] */
    for (int j = 0; j < 3; j++) {
        pthread_create(&thread[j], NULL, fn, NULL);
    }

    /* sanity checks that incorrect invocations return errors */
    f = fopen("/dummy-pseudo-fs", "r");
    if (f != NULL || errno != ENOENT) {
        perror("(sanity check) fopen of dummy non-existing pseudo-FS did not fail with ENOENT");
        return 1;
    }

    f = fopen("/proc", "r+");
    if (f != NULL || errno != EISDIR) {
        perror("(sanity check) fopen of /proc did not fail with EISDIR");
        return 1;
    }

    f = fopen("/proc/1", "r+");
    if (f != NULL || errno != EISDIR) {
        perror("(sanity check) fopen of /proc/1 did not fail with EISDIR");
        return 1;
    }

    f = fopen("/proc/2/cwd", "w");
    if (f != NULL || errno != EISDIR) {
        perror("(sanity check) fopen of /proc/2/cwd in write mode did not fail with EISDIR");
        return 1;
    }

    f = fopen("/proc/3/maps", "r+");
    if (f != NULL || errno != EACCES) {
        perror("(sanity check) fopen of /proc/3/maps in read-write mode did not fail with EACCES");
        return 1;
    }

    f = fopen("/proc/self/dummy", "r");
    if (f != NULL || errno != ENOENT) {
        perror("(sanity check) fopen of /proc/self/dummy (non-existing file) did not fail with "
               "ENOENT");
        return 1;
    }

    /* at this point, we must have /proc/1, /proc/2, /proc/3, /proc/4 */
    printf("===== Contents of /proc/1\n");
    dir = opendir("/proc/1");
    if (!dir) {
        perror("opendir /proc/1");
        return 1;
    }

    errno = 0;
    while ((dirent = readdir(dir))) {
        printf("/proc/1/%s\n", dirent->d_name);
    }
    if (errno) {
        perror("readdir /proc/1");
        return 1;
    }

    ret = closedir(dir);
    if (ret < 0) {
        perror("closedir /proc/1");
        return 1;
    }

    printf("===== Contents of /proc/1/fd\n");
    dir = opendir("/proc/1/fd");
    if (!dir) {
        perror("opendir /proc/1/fd");
        return 1;
    }

    errno = 0;
    while ((dirent = readdir(dir))) {
        printf("/proc/1/fd/%s\n", dirent->d_name);
    }
    if (errno) {
        perror("readdir /proc/1/fd");
        return 1;
    }

    ret = closedir(dir);
    if (ret < 0) {
        perror("closedir /proc/1/fd");
        return 1;
    }

    printf("===== Writing to /proc/1/fd/1 (stdout)\n");
    f = fopen("/proc/1/fd/1", "w");
    if (!f) {
        perror("fopen /proc/1/fd/1");
        return 1;
    }

    ret = fwrite(MSG, sizeof(MSG), 1, f);
    if (ferror(f)) {
        perror("fwrite /proc/1/fd/1");
        return 1;
    }

    /* above fwrite will print "Hello ..." to stdout *without* bufferization */
    memset(buf, 0, sizeof(buf));

    ret = fclose(f);
    if (ret) {
        perror("fclose /proc/1/fd/1");
        return 1;
    }

    printf("===== Writing to /dev/stdout (stdout)\n");
    f = fopen("/dev/stdout", "w");
    if (!f) {
        perror("fopen /dev/stdout");
        return 1;
    }

    ret = fwrite(MSG2, sizeof(MSG2), 1, f);
    if (ferror(f)) {
        perror("fwrite /dev/stdout");
        return 1;
    }

    /* above fwrite will print "Hello ..." to stdout *without* bufferization */
    memset(buf, 0, sizeof(buf));

    ret = fclose(f);
    if (ret) {
        perror("fclose /dev/stdout");
        return 1;
    }

    printf("===== Contents of /proc/self\n");
    dir = opendir("/proc/self");
    if (!dir) {
        perror("opendir /proc/self");
        return 1;
    }

    errno = 0;
    while ((dirent = readdir(dir))) {
        printf("/proc/self/%s\n", dirent->d_name);
    }
    if (errno) {
        perror("readdir /proc/self");
        return 1;
    }

    ret = closedir(dir);
    if (ret < 0) {
        perror("closedir /proc/self");
        return 1;
    }

    printf("===== Contents of /proc\n");
    dir = opendir("/proc");
    if (!dir) {
        perror("opendir /proc");
        return 1;
    }

    errno = 0;
    while ((dirent = readdir(dir))) {
        printf("/proc/%s, type: %d\n", dirent->d_name, dirent->d_type);
    }
    if (errno) {
        perror("readdir /proc");
        return 1;
    }

    ret = closedir(dir);
    if (ret < 0) {
        perror("closedir /proc");
        return 1;
    }

    printf("===== Reading /proc/self/exe symlink\n");
    int proc_dirfd = open("/proc", O_DIRECTORY | O_PATH | O_RDONLY);
    if (proc_dirfd < 0) {
        perror("open /proc");
        return 1;
    }

    ssize_t len = readlinkat(proc_dirfd, "self/exe", buf, sizeof(buf) - 1);
    if (len < 0) {
        perror("readlink /proc/self/exe");
        return 1;
    }

    if (close(proc_dirfd) < 0) {
        perror("close proc_dirfd");
        return 1;
    }

    buf[len] = '\0';
    printf("symlink /proc/self/exec resolves to %s\n", buf);

    /* this outputs all files in this current dir: a good test of realloced getdents buffer */
    printf("===== Contents of /proc/2/cwd\n");
    dir = opendir("/proc/2/cwd");
    if (!dir) {
        perror("opendir /proc/2/cwd");
        return 1;
    }

    errno = 0;
    while ((dirent = readdir(dir))) {
        printf("/proc/2/cwd/%s\n", dirent->d_name);
    }
    if (errno) {
        perror("readdir /proc/2/cwd");
        return 1;
    }

    ret = closedir(dir);
    if (ret < 0) {
        perror("closedir /proc/2/cwd");
        return 1;
    }

    printf("===== Contents of /proc/3/maps\n");
    f = fopen("/proc/3/maps", "r");
    if (!f) {
        perror("fopen /proc/3/maps");
        return 1;
    }

    memset(buf, 0, sizeof(buf));
    ret = fread(buf, 1, sizeof(buf), f);
    if (ferror(f)) {
        perror("fread /proc/3/maps");
        return 1;
    }

    printf("%s\n", buf);

    ret = fclose(f);
    if (ret) {
        perror("fclose /proc/3/maps");
        return 1;
    }

    printf("===== Contents of /proc/cpuinfo\n");
    f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        perror("fopen /proc/cpuinfo");
        return 1;
    }

    ret = fread(buf, 1, sizeof(buf) - 1, f);
    if (ferror(f)) {
        perror("fread /proc/cpuinfo");
        return 1;
    }
    buf[ret] = 0;

    printf("%s\n", buf);

    ret = fclose(f);
    if (ret) {
        perror("fclose /proc/cpuinfo");
        return 1;
    }

    return 0;
}
