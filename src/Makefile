CC = gcc

CFLAGS = -std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L
CDEBUGFLAGS = -fsanitize=address -ggdb
CRELEASEFLAGS = -O3

LINK3V2LIB = -L../id3v2lib/src -lid3v2lib
LINKRAYLIB = -I/usr/local/include -L../raylib/src -lraylib -lGL -lX11 -lm -lpthread -ldl -lrt
LINKDBUS = $(shell pkg-config --cflags --libs gio-2.0)

debug: main

all: main packer

release: main_release

main: main.o
	$(CC) $(CDEBUGFLAGS) -o $@ $^ $(LINKRAYLIB) $(LINK3V2LIB) $(LINKDBUS)

main_release: main_release.o
	$(CC) $(CRELEASEFLAGS) -o $@ $^ $(LINKRAYLIB) $(LINK3V2LIB) $(LINKDBUS)

main_release.o: main.c
	$(CC) $(CFLAGS) $(CRELEASEFLAGS) -c $< -o $@

packer: packer.o
	$(CC) $(CDEBUGFLAGS) -o $@ $^ $(LINKRAYLIB)

main.o: main.c
	$(CC) $(CFLAGS) $(CDEBUGFLAGS) -c $< -o $@

packer.o: packer.c
	$(CC) $(CFLAGS) $(CDEBUGFLAGS) -c $< -o $@



clean:
	rm -f main main_release packer *.o
