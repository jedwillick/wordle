#include "wordList.h"

#define INITIAL_LIST_CAPACITY 72

WordList* init_word_list(char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        perror("fopen");
        return NULL;
    }
    WordList* list = x_malloc(sizeof(WordList));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;
    list->words = x_malloc(sizeof(char*) * list->capacity);

    char* word;
    while ((word = read_line(file))) {
        if (!parse_word(word, -1, NULL)) {
            free(word);
            continue;
        }
        if (list->size == list->capacity - 1) {
            list->capacity *= 2;
            list->words = x_realloc(list->words,
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

bool in_list(WordList* list, char* word) {
    for (int i = 0; i < list->size; i++) {
        if (!strcmp(word, list->words[i])) {
            return true;
        }
    }
    return false;
}

char* parse_word(char* word, int wordLen, FILE* stream) {
    int i;
    for (i = 0; word[i]; i++) {
        // Removing any trailing newline.
        if (word[i] == '\n') {
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

char* get_random_word(WordList* list, int wordLen) {
    char* word;
    do {
        int i = rand() % list->size;
        word = list->words[i];
    } while (strlen(word) != wordLen);
    return strdup(word);
}
