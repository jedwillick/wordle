#ifndef UTIL_H
#define UTIL_H

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* x_strdup(char* str);
void* x_malloc(size_t size);
void* x_realloc(void* ptr, size_t size);
void* x_calloc(size_t nmemb, size_t size);
bool parse_int(int* dest, char* src);
char* read_line(FILE* file);
bool read_int(int* dest, FILE* to, FILE* from, char* msg, int min, int max);
void ignore_signals(int sigNums[]);

#endif  // UTIL_H
