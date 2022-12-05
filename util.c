#include "util.h"

#include <limits.h>
#include <signal.h>

#define INITIAL_BUFFER_SIZE 8

#define EXIT_OUT_MEM 99

char* x_strdup(char* str) {
    char* strDup = strdup(str);
    if (!strDup) {
        perror("strdup");
        exit(EXIT_OUT_MEM);
    }
    return strDup;
}

void* x_realloc(void* ptr, size_t size) {
    void* p = realloc(ptr, size);
    if (!p) {
        perror("realloc");
        exit(EXIT_OUT_MEM);
    }
    return p;
}

void* x_calloc(size_t nmemb, size_t size) {
    void* ptr = calloc(nmemb, size);
    if (!ptr) {
        perror("calloc");
        exit(EXIT_OUT_MEM);
    }
    return ptr;
}

void* x_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        perror("malloc");
        exit(EXIT_OUT_MEM);
    }
    return ptr;
}

/* parse_int()
 * −−−−−−−−−−−−−−−
 * Attempts to parse the src string as an integer and, if dest is not NULL,
 * places the result in dest. If the conversion was not successful the
 * value pointed to by dest is left unchanged. If underflow occurs dest is
 * set to INT_MIN. If overflow occurs dest is set to INT_MAX.
 *
 * dest: a pointer to the destination of the parsed integer.
 * src: the string being parsed.
 *
 * Returns: true if the conversion was successful, otherwise false.
 * Errors: if either dest or src are NULL no action is taken and false
 * is returned.
 */
bool parse_int(int* dest, char* src) {
    // Check for NULL and empty src string.
    if (!src || !*src) {
        return false;
    }

    char* end;
    long result = strtol(src, &end, 0);

    // Check for trailing characters.
    if (*end) {
        return false;
    }

    // Return early if not setting value.
    if (!dest) {
        return true;
    }

    // Check for under/over flow.
    if (result < INT_MIN) {
        *dest = INT_MIN;
    } else if (result > INT_MAX) {
        *dest = INT_MAX;
    } else {
        *dest = (int)result;
    }

    return true;
}

char* read_line(FILE* file) {
    if (!file) {
        return NULL;
    }
    size_t bufferSize = INITIAL_BUFFER_SIZE;
    char* buffer = malloc(bufferSize);
    if (!buffer) {
        return NULL;
    }
    int c, i;
    char* temp;
    for (i = 0; (c = fgetc(file)); i++) {
        if (c == EOF) {
            if (i == 0) {
                free(buffer);
                return NULL;
            }
            break;
        }
        if (c == '\n') {
            break;
        }
        if (i == bufferSize - 1) {
            bufferSize *= 2;  // Double strategy
            temp = buffer;
            buffer = realloc(buffer, bufferSize);
            if (!buffer) {
                free(temp);
                return NULL;
            }
        }
        buffer[i] = c;
    }
    buffer[i] = 0;
    return buffer;
}

bool read_int(int* dest, FILE* to, FILE* from, char* msg, int min, int max) {
    fprintf(to, "%s (%d to %d):\n", msg, min, max);
    fflush(to);
    char* input = read_line(from);
    if (!input) {
        return false;
    }
    if (parse_int(dest, input) && *dest >= min && *dest <= max) {
        free(input);
        return true;
    }
    free(input);
    return read_int(dest, to, from, msg, min, max);
}

void ignore_signals(int sigNums[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    for (int i = 0; sigNums[i]; i++) {
        sigaction(sigNums[i], &sa, NULL);
    }
}
