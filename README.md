# Wordle

My take on the game [wordle][nyt-wordle].  
Comes with two programs `wordle-server` and `wordle-client`

## Setup

```sh
https://github.com/jedwillick/wordle.git
cd wordle
make
```

## wordle-server

A multi-threaded TCP IPv4 server hosting wordle.
Multi-threading is implemented with the POSIX Threads (pthreads) library.

## wordle-client

A multi-threaded TCP IPv4 client that can be used to connect to the server.

[nyt-wordle]: https://www.nytimes.com/games/wordle/index.html
