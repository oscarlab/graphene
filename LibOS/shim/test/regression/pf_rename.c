#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char buf[15];
    char input_text[] = "Hello, world!";
    int fd = open("pftmp/foo.txt", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd < 0)
        err(1, "Cannot create file pftmp/foo.txt");

    int num_bytes = write(fd, input_text, strlen(input_text) + 1);
    if (num_bytes < 0) {
        close(fd);
        err(1, "Writing to file failed");
    }

    close(fd);

    int ret = rename("pftmp/foo.txt", "pftmp/bar.txt");
    if (ret < 0)
        err(1, "Rename failed");

    fd = open("pftmp/bar.txt", O_RDONLY);
    if (fd < 0)
        err(1, "Cannot open renamed file pftmp/bar.txt");

    num_bytes = read(fd, buf, sizeof(buf));
    close(fd);

    if (num_bytes < 0)
        err(1, "Reading from renamed file failed");

    buf[sizeof(buf) - 1] = '\0';

    if (strncmp(input_text, buf, sizeof(input_text)))
        err(1, "File content mismatching");

    printf("TEST OK\n");

    return 0;
}
