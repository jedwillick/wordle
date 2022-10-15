#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "util.h"

#define EXIT_OK              0
#define EXIT_BAD_USAGE       1
#define EXIT_CONNECTION_FAIL 3

#define NUM_ARGS 3

typedef struct Comms {
    FILE* input;
    FILE* output;
    bool fromServer;
} Comms;

int connect_to_server(char* hostname, char* port);
void communicate_with_server(int sockFd);
void* communicate_thread(void* args);
Comms* init_comms(FILE* input, FILE* output, bool fromServer);

int main(int argc, char** argv) {
    if (argc != NUM_ARGS) {
        fprintf(stderr, "Usage: wordle-client hostname port\n");
        return EXIT_BAD_USAGE;
    }

    ignore_signals((int[]){SIGPIPE, 0});

    char* hostname = argv[1];
    char* port = argv[2];
    int sockFd = connect_to_server(hostname, port);
    if (sockFd < 0) {
        fprintf(stderr, "wordle-client: unable to connect to %s port %s\n",
                hostname, port);
        return EXIT_CONNECTION_FAIL;
    }
    communicate_with_server(sockFd);
    return EXIT_OK;  // Will never reach here
}

Comms* init_comms(FILE* input, FILE* output, bool fromServer) {
    Comms* streams = malloc(sizeof(Comms));
    streams->input = input;
    streams->output = output;
    streams->fromServer = fromServer;
    return streams;
}

void communicate_with_server(int sockFd) {
    int sockFdDup = dup(sockFd);
    FILE* to = fdopen(sockFdDup, "w");
    FILE* from = fdopen(sockFd, "r");
    Comms* readFromServer = init_comms(from, stdout, true);
    Comms* writeToServer = init_comms(stdin, to, false);

    pthread_t tid;
    pthread_create(&tid, NULL, communicate_thread, readFromServer);
    pthread_detach(tid);
    communicate_thread(writeToServer);
}

void* communicate_thread(void* comms) {
    FILE* input = ((Comms*)comms)->input;
    FILE* output = ((Comms*)comms)->output;
    bool fromServer = ((Comms*)comms)->fromServer;
    free(comms);
    char* line;
    while ((line = read_line(input))) {
        if (fprintf(output, "%s\n", line) < 0 || fflush(output) == EOF) {
            exit(EXIT_CONNECTION_FAIL);
        }
        free(line);
    }
    if (fromServer) {
        printf("Server closed the connection\n");
    }
    exit(EXIT_OK);
    return NULL;  // Never reach here
}

int connect_to_server(char* hostname, char* port) {
    struct addrinfo* info = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    if (getaddrinfo(hostname, port, &hints, &info)) {
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(info);
        return -1;
    }
    if (connect(fd, (struct sockaddr*)info->ai_addr,
                sizeof(struct sockaddr))) {
        freeaddrinfo(info);
        close(fd);
        return -1;
    }
    freeaddrinfo(info);
    return fd;
}
