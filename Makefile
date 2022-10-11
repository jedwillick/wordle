CC = gcc
CFLAGS = -Wall -pedantic -std=gnu99
LDFLAGS =
LDLIBS =
PROGS = wordle

.PHONY: all debug clean

all: $(PROGS)

wordle: LDFLAGS += -pthread
wordle: wordle.o util.o wordList.o

wordle.o: CFLAGS += -pthread
wordle.o: wordle.c util.h wordList.h

util.o: util.c util.h

wordList.o: wordList.c wordList.h

debug: CFLAGS += -g
debug: clean all

clean:
	rm -f $(PROGS) *.o
