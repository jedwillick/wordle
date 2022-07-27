CC = gcc
CFLAGS = -Wall -pedantic -std=gnu99 $(INCLUDES)
INCLUDES =
LDFLAGS =
LDLIBS =
GCOV = -fprofile-arcs -ftest-coverage

all: wordle

wordle: CFLAGS += -pthread
wordle: LDFLAGS += -pthread
wordle: wordle.c

debug: CFLAGS += -g
debug: clean all

clean:
	rrm -f wordle
