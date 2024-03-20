#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(const char* name)
{
    fprintf(stderr, "USAGE: %s N T\n", name);
    fprintf(stderr, "N: 1 <= N - number of drivers\n");
    fprintf(stderr, "T: 5 <= T - simulation duration\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    (void)argc;
    usage(argv[0]);

    return EXIT_SUCCESS;
}