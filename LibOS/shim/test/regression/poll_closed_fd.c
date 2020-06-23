#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
    int ret;
    int pipefds[2];
    char buffer[1024];
    size_t bufsize = sizeof(buffer);
    int bytes;

    if (pipe(pipefds) < 0) {
        perror("pipe error\n");
        return 1;
    }

    int pid = fork();

    if (pid < 0) {
        perror("fork error\n");
        return 1;
    } else if (pid == 0) {
        /* client */
        close(pipefds[0]);

        snprintf(buffer, bufsize, "Hello from write end of pipe!");
        if (write(pipefds[1], &buffer, strlen(buffer) + 1) < 0) {
            perror("write error\n");
            return 1;
        }
    } else {
        /* server */
        close(pipefds[1]);

        struct pollfd infds[] = {
            {.fd = pipefds[0], .events = POLLIN},
        };
        for(;;) {
            ret = poll(infds, 1, -1);
            if (ret <= 0) {
                perror("poll with POLLIN failed\n");
                return 1;
            }

            if(0 != (infds[0].revents & POLLIN)) {
                bytes = read(pipefds[0], &buffer, bufsize);
                if (bytes  < 0) {
                    perror("read error\n");
                    return 1;
                } else if (bytes > 0) {
                    buffer[bytes] = '\0';
                    printf("read on pipe: %s\n", buffer);
                }
            }
            if(0 != (infds[0].revents & (POLLHUP | POLLERR | POLLNVAL))) {
                printf("the peer closed its end of the pipe\n");
                break;
            }
        }
        wait(NULL); /* wait for child termination, just for sanity */
    }

    return 0;
}
