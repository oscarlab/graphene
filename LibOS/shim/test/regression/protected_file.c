/* This test opens the same file twice, writes to one FD, reads from another FD, and closes both
 * FDs. This test exists mainly to test Protected Files Linux-SGX feature. */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_LENGTH 256
#define STRING "Hello World"

static ssize_t rw_file(int fd, char* buf, size_t bytes, bool write_flag) {
    ssize_t rv = 0;
    ssize_t ret;

    while (bytes > rv) {
        if (write_flag)
            ret = write(fd, buf + rv, bytes - rv);
        else
            ret = read(fd, buf + rv, bytes - rv);

        if (ret > 0) {
            rv += ret;
        } else {
            if (ret < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
            } else {
                fprintf(stderr, "%s failed:%s\n", write_flag ? "write" : "read", strerror(errno));
                return ret;
            }
        }
    }

    return rv;
}

int main(int argc, char** argv) {
    int ret;
    char buf[BUF_LENGTH] = {0};
    ssize_t bytes;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s protected_file_path\n", argv[0]);
        return 1;
    }

    char* protected_file_path = argv[1];
    int fd1 = open(protected_file_path, O_CREAT | O_RDONLY, 0644);
    if (fd1 < 0)
        err(1, "open of first fd");

    int fd2 = open(protected_file_path, O_CREAT | O_RDWR, 0644);
    if (fd2 < 0)
        err(1, "open of second fd");

    bytes = rw_file(fd2, STRING, sizeof(STRING), /*write_flag=*/true);
    if (bytes != sizeof(STRING))
        errx(1, "writing '" STRING "' to second fd failed");

    bytes = rw_file(fd1, buf, sizeof(STRING), /*write_flag=*/false);
    if (bytes < 0)
        errx(1, "reading '" STRING "' from first fd failed");

    buf[bytes - 1] = '\0';

    if (strcmp(STRING, buf))
        errx(1, "unexpected '%s' was read", buf);

    ret = close(fd2);
    if (ret < 0)
        err(1, "close of second fd");

    ret = close(fd1);
    if (ret < 0)
        err(1, "close of first fd");

    puts("TEST OK");
    return 0;
}
