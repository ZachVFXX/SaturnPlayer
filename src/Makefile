CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L
CDEBUGFLAGS = -fsanitize=address -ggdb
CRELEASEFLAGS = -O3

FFMPEG_DIR = $(HOME)/ffmpeg-static
FFMPEG_CFLAGS = -I$(FFMPEG_DIR)/include
FFMPEG_LIBS = $(FFMPEG_DIR)/lib/libavformat.a \
              $(FFMPEG_DIR)/lib/libavcodec.a \
              $(FFMPEG_DIR)/lib/libavutil.a \
              $(FFMPEG_DIR)/lib/libswresample.a \
              -llzma -lbz2 -ldrm -lz -lm -lpthread

LINKRAYLIB = -I/usr/local/include -L../raylib/src -lraylib -lGL -lX11 -lm -lpthread -ldl -lrt
LINKDBUS = $(shell pkg-config --cflags --libs gio-2.0)

debug: main

all: main packer

release: main_release

main: main.o
	$(CC) $(CDEBUGFLAGS) -o $@ $^ $(LINKRAYLIB) $(FFMPEG_LIBS)

main_release: main_release.o
	$(CC) $(CRELEASEFLAGS) -s -o $@ $^ $(LINKRAYLIB) $(FFMPEG_LIBS)

main_release.o: main.c
	$(CC) $(CFLAGS) $(CRELEASEFLAGS) $(FFMPEG_CFLAGS) -c $< -o $@

packer: packer.o
	$(CC) $(CDEBUGFLAGS) -o $@ $^ $(LINKRAYLIB)

main.o: main.c
	$(CC) $(CFLAGS) $(CDEBUGFLAGS) $(FFMPEG_CFLAGS) -c $< -o $@

packer.o: packer.c
	$(CC) $(CFLAGS) $(CDEBUGFLAGS) -c $< -o $@

clean:
	rm -f main main_release packer *.o
