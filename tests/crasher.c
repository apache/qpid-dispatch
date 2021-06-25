#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Missing argument!\n");
        return EXIT_FAILURE;
    }

    char *arg = argv[1];
    if (strcmp(arg, "success") == 0) {
        return EXIT_SUCCESS;
    } else if (strcmp(arg, "failure") == 0) {
        return EXIT_FAILURE;
    } else if (strcmp(arg, "segfault") == 0) {
        int *a = NULL;
        *a = 42;
        fprintf(stderr, "Failed to segfault!\n");
        return EXIT_FAILURE;
    } else if (strcmp(arg, "abort") == 0) {
        abort();
        fprintf(stderr, "Failed to abort!\n");
        return EXIT_FAILURE;
    } else {
        fprintf(stderr, "Invalid argument!\n");
        return EXIT_FAILURE;
    }
}
