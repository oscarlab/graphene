#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SECRETSTRING "Secret string\n"

static ssize_t rw_file(const char* path, char* buf, size_t bytes, bool do_write) {
    size_t rv = 0;
    size_t ret = 0;

    FILE* f = fopen(path, do_write ? "w" : "r");
    if (!f) {
        fprintf(stderr, "opening %s failed\n", path);
        return -1;
    }

    while (bytes > rv) {
        if (do_write)
            ret = fwrite(buf + rv, /*size=*/1, /*nmemb=*/bytes - rv, f);
        else
            ret = fread(buf + rv, /*size=*/1, /*nmemb=*/bytes - rv, f);

        if (ret > 0) {
            rv += ret;
        } else {
            if (feof(f)) {
                if (rv) {
                    /* read some bytes from file, success */
                    break;
                }
                assert(rv == 0);
                fprintf(stderr, "%s failed: unexpected end of file\n", do_write ? "write" : "read");
                fclose(f);
                return -1;
            }

            assert(ferror(f));

            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }

            fprintf(stderr, "%s failed: %s\n", do_write ? "write" : "read", strerror(errno));
            fclose(f);
            return -1;
        }
    }

    int close_ret = fclose(f);
    if (close_ret) {
        fprintf(stderr, "closing %s failed\n", path);
        return -1;
    }
    return rv;
}


int main(int argc, char** argv) {
    int ret;
    ssize_t bytes;

    if (argc != 2)
        errx(EXIT_FAILURE, "Usage: %s <protected file to create/validate>", argv[0]);

    ret = access(argv[1], F_OK);
    if (ret < 0) {
        if (errno == ENOENT) {
            /* file is not yet created, create with secret string */
            bytes = rw_file(argv[1], SECRETSTRING, sizeof(SECRETSTRING), /*do_write=*/true);
            if (bytes != sizeof(SECRETSTRING)) {
                /* error is already printed by rw_file_f() */
                return EXIT_FAILURE;
            }
            printf("CREATION OK\n");
            return 0;
        }
        err(EXIT_FAILURE, "access failed");
    }

    char buf[128];
    bytes = rw_file(argv[1], buf, sizeof(buf), /*do_write=*/false);
    if (bytes <= 0) {
        /* error is already printed by rw_file_f() */
        return EXIT_FAILURE;
    }
    buf[bytes - 1] = '\0';

    size_t size_to_cmp = sizeof(SECRETSTRING) < bytes ? sizeof(SECRETSTRING) : bytes;
    if (strncmp(SECRETSTRING, buf, size_to_cmp))
        errx(EXIT_FAILURE, "Expected '%s' but read '%s'\n", SECRETSTRING, buf);

    printf("TEST OK\n");
    return 0;
}
