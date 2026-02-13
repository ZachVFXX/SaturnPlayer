CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L
CDEBUGFLAGS = -fsanitize=address -ggdb
CRELEASEFLAGS = -O3
LINKAVFORMAT = $(shell pkg-config --cflags --libs libavformat libavcodec libavutil)
LINKRAYLIB = -I/usr/local/include -L../raylib/src -lraylib -lGL -lX11 -lm -lpthread -ldl -lrt
LINKDBUS = $(shell pkg-config --cflags --libs gio-2.0)

debug: main

all: main packer

release: main_release

main: main.o
	$(CC) $(CDEBUGFLAGS) -o $@ $^ $(LINKRAYLIB) $(LINKAVFORMAT) $(LINKDBUS)

main_release: main_release.o
	$(CC) $(CRELEASEFLAGS) -o $@ $^ $(LINKRAYLIB) $(LINKAVFORMAT) $(LINKDBUS)

main_release.o: main.c
	$(CC) $(CFLAGS) $(CRELEASEFLAGS) $(LINKAVFORMAT) -c $< -o $@

main.o: main.c
	$(CC) $(CFLAGS) $(CDEBUGFLAGS) $(LINKAVFORMAT) -c $< -o $@

packer: packer.o
	$(CC) $(CDEBUGFLAGS) -o $@ $^ $(LINKRAYLIB)

packer.o: packer.c
	$(CC) $(CFLAGS) $(CDEBUGFLAGS) -c $< -o $@

clean:
	rm -f main main_release packer *.o
