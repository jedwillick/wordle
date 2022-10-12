CC = gcc
CFLAGS = -Wall -pedantic -std=gnu99
LDFLAGS =
LDLIBS =
PROGS = wordle-server wordle-client

.PHONY: all debug clean

all: $(PROGS)

wordle-client: LDFLAGS += -pthread
wordle-client: wordleClient.o util.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

wordle-server: LDFLAGS += -pthread
wordle-server: wordleServer.o util.o wordList.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

wordleClient.o: CFLAGS += -pthread
wordleClient.o: wordleClient.c util.h

wordleServer.o: CFLAGS += -pthread
wordleServer.o: wordleServer.c util.h wordList.h

util.o: util.c util.h

wordList.o: wordList.c wordList.h

debug: CFLAGS += -g
debug: clean all

clean:
	rm -f $(PROGS) *.o
