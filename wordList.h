#ifndef WORD_LIST_H
#define WORD_LIST_H

#include "util.h"

typedef struct {
    char** words;
    size_t size;
    size_t capacity;
} WordList;

WordList* init_word_list(char* path);
void free_word_list(WordList* list);
bool in_list(WordList* list, char* word);
char* parse_word(char* word, int wordLen, FILE* stream);
char* get_random_word(WordList* list, int wordLen);

#endif  // WORD_LIST_H
