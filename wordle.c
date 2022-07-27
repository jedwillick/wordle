#include <arpa/inet.h>
#include <ctype.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_ARGC 7
#define MIN_ARGC 1

#define DEFAULT_ANSWERS_PATH "default-answers.txt"
#define DEFAULT_GUESSES_PATH "default-guesses.txt"
#define DEFAULT_HOSTNAME     NULL  // Listen on all hosts
#define DEFAULT_PORT         "0"   // Ephemeral portd

#define EXIT_OK              0
#define EXIT_BAD_USAGE       1
#define EXIT_FNF             2
#define EXIT_CONNECTION_FAIL 3
#define EXIT_OUT_MEM         4

#define INITIAL_BUFFER_SIZE   8
#define INITIAL_LIST_CAPACITY 72
#define STRFTIME_BUFFER       52

#define MIN_TRIES     1
#define MAX_TRIES     10
#define DEFAULT_TRIES 6

#define MIN_WORD_LEN     3
#define MAX_WORD_LEN     9
#define DEFAULT_WORD_LEN 5

#define CMD_OPTION  '-'
#define WRONG_GUESS '-'
#define IP_DELIM    '.'
#define NEWLINE     '\n'

#define MAX_BACKLOG 128

#define FATAL_ERROR_MSG "A fatal error occured :(. Try again later\n"

typedef struct {
    char** words;
    size_t size;
    size_t capacity;
} WordList;

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

char* s_strdup(char* str);
void* s_malloc(size_t size);
void* s_realloc(void* ptr, size_t size);
void* s_calloc(size_t nmemb, size_t size);
bool parse_int(int* dest, char* src);
void usage_exit(void);
void free_server_details(ServerDetails* details);
char* parse_word(char* word, int wordLen, FILE* stream);
WordList* init_word_list(char* path);
void free_word_list(WordList* list);
ServerDetails* parse_arguments(int argc, char** argv);
bool open_server(ServerDetails* details);
char* get_random_word(WordList* list, int wordLen);
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
bool in_list(WordList* list, char* word);
char* read_line(FILE* file);
void game_menu(FILE* to, FILE* from, ServerDetails* details,
        ServerStats* stats);
bool read_int(int* dest, FILE* to, FILE* from, char* msg, int min, int max);
void print_welcome(FILE* to);

/* Wordle Server
 * −−−−−−−−−−−−−−−
 * Usage: ./wordle [-answers file] [-guesses file] [hostname] [port]
 */
int main(int argc, char** argv) {
    ServerDetails* details = parse_arguments(argc, argv);
    ServerStats* stats = init_server_stats();

    // Ignore SIGPIPE
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGPIPE, &sa, NULL);

    if (!open_server(details)) {
        fprintf(stderr, "wordle: unable to connect to %s port %s\n",
                details->hostname, details->port);
        free_server_details(details);
        free_server_stats(stats);
        return EXIT_CONNECTION_FAIL;
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
            write(fd, FATAL_ERROR_MSG, strlen(FATAL_ERROR_MSG));
            close(fd);
            continue;
        }
        wrap->fd = malloc(sizeof(int));
        if (!wrap->fd) {
            write(fd, FATAL_ERROR_MSG, strlen(FATAL_ERROR_MSG));
            close(fd);
            continue;
        }
        *wrap->fd = fd;
        wrap->details = details;
        wrap->stats = stats;

        // Create and detach client handling thread.
        if (pthread_create(&tid, NULL, client_thread, wrap)) {
            write(fd, FATAL_ERROR_MSG, strlen(FATAL_ERROR_MSG));
            close(fd);
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
    bool won;
    print_welcome(to);
    while (true) {
        fprintf(to, "Select one of the following:\n");
        fprintf(to, "1. Play game (word length: %d, tries: %d)\n", wordLen,
                tries);
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
                if (!answer) {
                    answer = get_random_word(details->answers, wordLen);
                }
                won = play_game(to, from, details, wordLen, tries, answer);
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

char* get_random_word(WordList* list, int wordLen) {
    char* word;
    do {
        int i = rand() % list->size;
        word = list->words[i];
    } while (strlen(word) != wordLen);
    return word;
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
        if (c == NEWLINE) {
            break;
        }
        if (i == bufferSize - 1) {
            bufferSize *= 2;
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
        if (tries) {
            print_prompt(to, wordLen, tries);
        }
        free(guess);
    }
    fprintf(to, "Bad luck - the word is \"%s\".\n", answer);
    return false;
}

bool in_list(WordList* list, char* word) {
    for (int i = 0; i < list->size; i++) {
        if (!strcmp(word, list->words[i])) {
            return true;
        }
    }
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
        raw = time(NULL);
        local = localtime(&raw);
        len = strftime(buffer, STRFTIME_BUFFER, "%c", local);
        pthread_mutex_lock(&stats->lock);
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
    ServerStats* stats = s_calloc(1, sizeof(ServerStats));
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

    if (listen(fd, MAX_BACKLOG)) {
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
    if (argc < MIN_ARGC || argc > MAX_ARGC) {
        usage_exit();
    }
    char* answersPath = DEFAULT_ANSWERS_PATH;
    char* guessesPath = DEFAULT_GUESSES_PATH;
    char* hostname = DEFAULT_HOSTNAME;
    char* port = DEFAULT_PORT;

    bool hostnameFound = false, portFound = false;

    // Starting at 1 to avoid program name
    for (int i = 1; i < argc; i++) {
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
            if (parse_int(NULL, argv[i]) && !portFound) {
                port = argv[i];
                portFound = true;
            } else if (!hostnameFound) {
                hostname = argv[i];
                hostnameFound = true;
            } else {
                usage_exit();
            }
        }
    }
    ServerDetails* details = s_malloc(sizeof(ServerDetails));
    details->hostname = hostname;
    details->port = port;
    details->answers = init_word_list(answersPath);
    if (!details->answers) {
        details->guesses = NULL;
        free_server_details(details);
        exit(EXIT_FNF);
    }
    details->guesses = init_word_list(guessesPath);
    if (!details->guesses) {
        free_server_details(details);
        exit(EXIT_FNF);
    }
    details->fd = -1;
    return details;
}

char* parse_word(char* word, int wordLen, FILE* stream) {
    int i;
    for (i = 0; word[i]; i++) {
        // Removing any trailing newline.
        if (word[i] == NEWLINE) {
            word[i] = 0;
            i--;
            continue;
        }

        if (!isalpha(word[i])) {
            if (stream) {
                fprintf(stream,
                        "Words must contain only letters - try again.\n");
            }
            return NULL;
        }
        word[i] = tolower(word[i]);
    }
    if (wordLen >= 0 && i != wordLen) {
        if (stream) {
            fprintf(stream, "Words must be %d letters long - try again.\n",
                    wordLen);
        }
        return NULL;
    }
    return word;
}

WordList* init_word_list(char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        perror("fopen");
        return NULL;
    }
    WordList* list = s_malloc(sizeof(WordList));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;
    list->words = s_malloc(sizeof(char*) * list->capacity);

    char* word;
    while ((word = read_line(file))) {
        if (!parse_word(word, -1, NULL)) {
            free(word);
            continue;
        }
        if (list->size == list->capacity - 1) {
            list->capacity *= 2;
            list->words = s_realloc(list->words,
                    sizeof(char*) * list->capacity);
        }
        list->words[list->size] = word;
        list->size++;
    }
    fclose(file);
    return list;
}

void free_word_list(WordList* list) {
    if (!list) {
        return;
    }
    for (int i = 0; i < list->size; i++) {
        free(list->words[i]);
    }
    free(list->words);
    free(list);
}

void free_server_details(ServerDetails* details) {
    if (!details) {
        return;
    }
    free_word_list(details->answers);
    free_word_list(details->guesses);
    free(details);
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

void usage_exit(void) {
    fprintf(stderr, "Usage: ./wordle [-answers file] [-guesses file] "
                    "[hostname] [port]\n");
    exit(EXIT_BAD_USAGE);
}

char* s_strdup(char* str) {
    char* strDup = strdup(str);
    if (!strDup) {
        perror("strdup");
        exit(EXIT_OUT_MEM);
    }
    return strDup;
}

void* s_realloc(void* ptr, size_t size) {
    void* p = realloc(ptr, size);
    if (!p) {
        perror("realloc");
        exit(EXIT_OUT_MEM);
    }
    return p;
}

void* s_calloc(size_t nmemb, size_t size) {
    void* ptr = calloc(nmemb, size);
    if (!ptr) {
        perror("calloc");
        exit(EXIT_OUT_MEM);
    }
    return ptr;
}

void* s_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        perror("malloc");
        exit(EXIT_OUT_MEM);
    }
    return ptr;
}
