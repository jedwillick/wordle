#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "util.h"
#include "wordList.h"

#define DEFAULT_ANSWERS_PATH "default-answers.txt"
#define DEFAULT_GUESSES_PATH "default-guesses.txt"
#define DEFAULT_HOSTNAME     NULL  // Listen on all hosts
#define DEFAULT_PORT         "0"   // Ephemeral port

#define EXIT_OK          0
#define EXIT_BAD_USAGE   1
#define EXIT_FNF         2
#define EXIT_LISTEN_FAIL 3

#define STRFTIME_BUFFER 52

#define MIN_TRIES     1
#define MAX_TRIES     10
#define DEFAULT_TRIES 6

#define MIN_WORD_LEN     3
#define MAX_WORD_LEN     9
#define DEFAULT_WORD_LEN 5

#define CMD_OPTION  '-'
#define WRONG_GUESS '-'
#define IP_DELIM    '.'

typedef struct {
    WordList* answers;
    WordList* guesses;
    char* hostname;
    char* port;
    int fd;
} ServerDetails;

typedef struct {
    int connected;
    int completed;
    int won;
    int lost;
    pthread_mutex_t lock;
    sigset_t set;
} ServerStats;

typedef struct {
    ServerStats* stats;
    ServerDetails* details;
    int* fd;
} Wrapper;

void usage_exit(void);
void free_server_details(ServerDetails* details);
ServerDetails* parse_arguments(int argc, char** argv);
bool open_server(ServerDetails* details);
bool print_server_port(ServerDetails* details);
ServerStats* init_server_stats(void);
void free_server_stats(ServerStats* stats);
void* stats_thread(void* rawStats);
void process_connections(ServerDetails* details, ServerStats* stats);
void* client_thread(void* wrapper);
void increment_stat(int* stat, pthread_mutex_t* lock);
void print_prompt(FILE* stream, int wordLen, int tries);
bool play_game(FILE* to, FILE* from, ServerDetails* details, int wordLen,
        int tries, char* answer);
char* get_hint(char* guess, char* answer, int wordLen);
void game_menu(FILE* to, FILE* from, ServerDetails* details,
        ServerStats* stats);
void print_welcome(FILE* to);
void fatal_server_error(int socketfd);

/* Wordle Server
 * −−−−−−−−−−−−−−−
 * Usage: ./wordle-server [-answers file] [-guesses file] [hostname] [port]
 */
int main(int argc, char** argv) {
    ServerDetails* details = parse_arguments(argc, argv);
    ServerStats* stats = init_server_stats();

    ignore_signals((int[]){SIGPIPE, 0});

    if (!open_server(details)) {
        fprintf(stderr, "wordle-server: unable to listen on %s port %s\n",
                details->hostname, details->port);
        free_server_details(details);
        free_server_stats(stats);
        return EXIT_LISTEN_FAIL;
    }
    srand(time(NULL));
    process_connections(details, stats);

    free_server_details(details);
    free_server_stats(stats);
    return EXIT_OK;
}

void process_connections(ServerDetails* details, ServerStats* stats) {
    int fd;
    pthread_t tid;
    while (true) {
        fd = accept(details->fd, NULL, NULL);
        if (fd < 0) {
            continue;
        }

        Wrapper* wrap = malloc(sizeof(Wrapper));
        if (!wrap) {
            fatal_server_error(fd);
            continue;
        }
        wrap->fd = malloc(sizeof(int));
        if (!wrap->fd) {
            fatal_server_error(fd);
            continue;
        }
        *wrap->fd = fd;
        wrap->details = details;
        wrap->stats = stats;

        // Create and detach client handling thread.
        if (pthread_create(&tid, NULL, client_thread, wrap)) {
            fatal_server_error(fd);
            continue;
        }
        pthread_detach(tid);
    }
}

void increment_stat(int* stat, pthread_mutex_t* lock) {
    pthread_mutex_lock(lock);
    (*stat)++;
    pthread_mutex_unlock(lock);
}
void* client_thread(void* wrapper) {
    // Unwrap the argument.
    Wrapper* wrap = wrapper;
    int fd = *wrap->fd;
    ServerDetails* details = wrap->details;
    ServerStats* stats = wrap->stats;
    free(wrap->fd);
    free(wrap);

    increment_stat(&stats->connected, &stats->lock);
    int fdDup = dup(fd);
    FILE* to = fdopen(fdDup, "w");
    FILE* from = fdopen(fd, "r");

    game_menu(to, from, details, stats);

    fclose(to);
    fclose(from);

    pthread_mutex_lock(&stats->lock);
    stats->connected--;
    stats->completed++;
    pthread_mutex_unlock(&stats->lock);
    return NULL;
}

void print_welcome(FILE* to) {
    fprintf(to, "Welcome to...\n");
    fprintf(to, " _    _               _ _      \n");
    fprintf(to, "| |  | |             | | |     \n");
    fprintf(to, "| |  | | ___  _ __ __| | | ___ \n");
    fprintf(to, "| |/\\| |/ _ \\| '__/ _` | |/ _ \\\n");
    fprintf(to, "\\  /\\  / (_) | | | (_| | |  __/\n");
    fprintf(to, " \\/  \\/ \\___/|_|  \\__,_|_|\\___|\n\n");
    fflush(to);
}

void game_menu(FILE* to, FILE* from, ServerDetails* details,
        ServerStats* stats) {
    char* input;
    char* answer = NULL;
    int option, wordLen = DEFAULT_WORD_LEN, tries = DEFAULT_TRIES, streak = 0;
    bool won = false;
    print_welcome(to);
    while (true) {
        fprintf(to, "Select one of the following:\n");
        fprintf(to, "1. Play game (word length: %d, tries: %d, answer: %s)\n",
                wordLen, tries, answer ? answer : "?????");
        fprintf(to, "2. Change word length\n");
        fprintf(to, "3. Change number of tries\n");
        fprintf(to, "4. Cheat and set the answer\n");
        fprintf(to, "5. Exit\n");
        fflush(to);
        if (!(input = read_line(from))) {
            return;
        }
        if (!parse_int(&option, input)) {
            free(input);
            continue;
        }
        free(input);
        switch (option) {
            case 1:
                if (!answer && !(answer = get_random_word(details->answers,
                                         wordLen))) {
                    perror("get_random_word");
                    return;
                }
                won = play_game(to, from, details, wordLen, tries, answer);
                free(answer);
                answer = NULL;
                increment_stat(won ? &stats->won : &stats->lost, &stats->lock);
                streak = won ? streak + 1 : 0;
                fprintf(to, "Win Streak: %d\n\n", streak);
                break;
            case 2:
                if (!read_int(&wordLen, to, from, "Enter the word length",
                            MIN_WORD_LEN, MAX_WORD_LEN)) {
                    return;
                }
                break;
            case 3:
                if (!read_int(&tries, to, from, "Enter the number of tries",
                            MIN_TRIES, MAX_TRIES)) {
                    return;
                }
                break;
            case 4:
                fprintf(to, "Enter the answer word:\n");
                fflush(to);
                free(answer);
                if (!(answer = read_line(from))) {
                    return;
                }
                if (answer[0] == 0) {
                    answer = NULL;
                    wordLen = DEFAULT_WORD_LEN;
                } else {
                    wordLen = strlen(answer);
                }
                break;
            case 5:
                fprintf(to, "Goodbye...\n");
                return;
        }
    }
}

bool play_game(FILE* to, FILE* from, ServerDetails* details, int wordLen,
        int tries, char* answer) {
    print_prompt(to, wordLen, tries);
    char *guess, *hint;
    while (tries && (guess = read_line(from))) {
        if (parse_word(guess, wordLen, to)) {
            if (!strcmp(guess, answer)) {
                fprintf(to, "Correct!\n");
                free(guess);
                return true;
            }
            if (in_list(details->guesses, guess)) {
                hint = get_hint(guess, answer, wordLen);
                fprintf(to, "%s\n", hint);
                free(hint);
                tries--;
            } else {
                fprintf(to, "Word not found in the dictionary - try again.\n");
            }
        }
        free(guess);
        print_prompt(to, wordLen, tries);
    }
    fprintf(to, "Bad luck - the word is \"%s\".\n", answer);
    return false;
}

char* get_hint(char* guess, char* answer, int wordLen) {
    char* hint = calloc(wordLen + 1, sizeof(char));

    // Setting the correct characters.
    for (int i = 0; i < wordLen; i++) {
        if (answer[i] == guess[i]) {
            hint[i] = toupper(guess[i]);
        }
    }

    int letterCount, displayedCount;
    // Dealing with the wrong letters and letters not in the right position
    for (int i = 0; i < wordLen; i++) {
        if (!hint[i]) {
            letterCount = 0;
            displayedCount = 0;
            for (int j = 0; j < wordLen; j++) {
                // Counting the number of times the letter appears in the
                // answer and how many times it appears in the hint.
                if (guess[i] == answer[j]) {
                    letterCount++;
                }
                if (guess[i] == tolower(hint[j])) {
                    displayedCount++;
                }
            }
            // Ensuring that the letter doesn't appear in the hint more
            // than the answer
            if (displayedCount < letterCount && strchr(answer, guess[i])) {
                hint[i] = guess[i];
                displayedCount++;
            } else {
                hint[i] = WRONG_GUESS;
            }
        }
    }
    return hint;
}

void print_prompt(FILE* stream, int wordLen, int tries) {
    if (tries <= 0) {
        return;
    }
    fprintf(stream, "Enter a %d letter word ", wordLen);
    if (tries == 1) {
        fprintf(stream, "(last attempt):\n");
    } else {
        fprintf(stream, "(%d attempts remaining):\n", tries);
    }
    fflush(stream);
}

void* stats_thread(void* rawStats) {
    ServerStats* stats = rawStats;
    int sigNum;
    time_t raw;
    struct tm* local;
    char buffer[STRFTIME_BUFFER];
    size_t len;
    while (true) {
        sigwait(&stats->set, &sigNum);
        pthread_mutex_lock(&stats->lock);
        raw = time(NULL);
        local = localtime(&raw);
        len = strftime(buffer, STRFTIME_BUFFER, "%c", local);
        fprintf(stderr, "Server Stats at %s\n", len ? buffer : "????");
        fprintf(stderr, "Connected clients: %d\n", stats->connected);
        fprintf(stderr, "Completed clients: %d\n", stats->completed);
        fprintf(stderr, "Games won:         %d\n", stats->won);
        fprintf(stderr, "Games lost:        %d\n", stats->lost);
        fflush(stderr);
        pthread_mutex_unlock(&stats->lock);
    }
    return NULL;
}

ServerStats* init_server_stats(void) {
    ServerStats* stats = x_calloc(1, sizeof(ServerStats));
    pthread_mutex_init(&stats->lock, NULL);
    sigemptyset(&stats->set);
    sigaddset(&stats->set, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &stats->set, NULL);

    // Create and detach SIGHUP handling thread.
    pthread_t tid;
    pthread_create(&tid, NULL, stats_thread, stats);
    pthread_detach(tid);
    return stats;
}

void free_server_stats(ServerStats* stats) {
    pthread_mutex_destroy(&stats->lock);
    free(stats);
}

bool open_server(ServerDetails* details) {
    struct addrinfo* info = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(details->hostname, details->port, &hints, &info)) {
        return false;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(info);
        return false;
    }

    // Allow address (port number) to be reused immediately
    int optVal = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(int))) {
        freeaddrinfo(info);
        return false;
    }

    if (bind(fd, (struct sockaddr*)info->ai_addr, sizeof(struct sockaddr))) {
        freeaddrinfo(info);
        return false;
    }
    freeaddrinfo(info);

    if (listen(fd, SOMAXCONN)) {
        return false;
    }
    details->fd = fd;
    return print_server_port(details);
}

bool print_server_port(ServerDetails* details) {
    char* hostname = details->hostname ? details->hostname : "ALL";
    fprintf(stderr, "Listening on %s port ", hostname);
    // Check for ephemeral port.
    if (!strcmp(details->port, "0")) {
        struct sockaddr_in ad;
        memset(&ad, 0, sizeof(struct sockaddr_in));
        socklen_t len = sizeof(struct sockaddr_in);

        // Resolve port number from socket fd.
        if (getsockname(details->fd, (struct sockaddr*)&ad, &len)) {
            return false;
        }

        fprintf(stderr, "%u\n", ntohs(ad.sin_port));
    } else {
        fprintf(stderr, "%s\n", details->port);
    }
    fflush(stderr);
    return true;
}

ServerDetails* parse_arguments(int argc, char** argv) {
    char* answersPath = DEFAULT_ANSWERS_PATH;
    char* guessesPath = DEFAULT_GUESSES_PATH;
    char* hostname = DEFAULT_HOSTNAME;
    char* port = DEFAULT_PORT;

    bool hostnameFound = false, portFound = false;

    // Starting at 1 to avoid program name
    for (int i = 1; argv[i]; i++) {
        if (argv[i][0] == CMD_OPTION) {
            if (i + 1 >= argc) {
                usage_exit();
            } else if (!strcmp(argv[i], "-answers")) {
                answersPath = argv[++i];
            } else if (!strcmp(argv[i], "-guesses")) {
                guessesPath = argv[++i];
            } else {
                usage_exit();
            }
        } else {  // hostname or port
            if (!hostnameFound) {
                hostname = argv[i];
                hostnameFound = true;
            } else if (!portFound) {
                port = argv[i];
                portFound = true;
            } else {
                usage_exit();
            }
        }
    }
    ServerDetails* details = x_calloc(1, sizeof(ServerDetails));
    details->hostname = hostname;
    details->port = port;
    details->answers = init_word_list(answersPath);
    details->guesses = init_word_list(guessesPath);
    if (!details->answers || !details->guesses) {
        free_server_details(details);
        exit(EXIT_FNF);
    }
    details->fd = -1;
    return details;
}

void free_server_details(ServerDetails* details) {
    if (!details) {
        return;
    }
    free_word_list(details->answers);
    free_word_list(details->guesses);
    free(details);
}

void usage_exit(void) {
    fprintf(stderr, "Usage: wordle-server [-answers file] [-guesses file] "
                    "[hostname] [port]\n");
    exit(EXIT_BAD_USAGE);
}

void fatal_server_error(int socketfd) {
    char* msg = "A fatal server error occured :(. Try again later\n";
    write(socketfd, msg, strlen(msg));
    close(socketfd);
}
