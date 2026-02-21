CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L
CDEBUGFLAGS = -fsanitize=address -ggdb
CRELEASEFLAGS = -O3

FREETYPE_CFLAGS = -I/usr/include/freetype2
FREETYPE_LIBS   = -lfreetype
FONTCONFIG_LIBS = -lfontconfig

FFMPEG_DIR = $(HOME)/ffmpeg-static
FFMPEG_CFLAGS = -I$(FFMPEG_DIR)/include
FFMPEG_LIBS = $(FFMPEG_DIR)/lib/libavformat.a \
              $(FFMPEG_DIR)/lib/libavcodec.a \
              $(FFMPEG_DIR)/lib/libavutil.a \
              $(FFMPEG_DIR)/lib/libswresample.a \
              -llzma -lbz2 -ldrm -lz -lm -lpthread

LINKRAYLIB = -I/usr/local/include -L../raylib/src -lraylib -lGL -lX11 -lm -lpthread -ldl -lrt

ALL_CFLAGS  = $(CFLAGS) $(FFMPEG_CFLAGS) $(FREETYPE_CFLAGS)
ALL_LIBS = $(LINKRAYLIB) $(FFMPEG_LIBS) $(FREETYPE_LIBS) $(FONTCONFIG_LIBS)

debug: main
all: main packer
release: main_release

main: main.o
	$(CC) $(CDEBUGFLAGS) -o $@ $^ $(ALL_LIBS)

main_release: main_release.o
	$(CC) $(CRELEASEFLAGS) -s -o $@ $^ $(ALL_LIBS)

main_release.o: main.c
	$(CC) $(ALL_CFLAGS) $(CRELEASEFLAGS) -c $< -o $@

main.o: main.c
	$(CC) $(ALL_CFLAGS) $(CDEBUGFLAGS) -c $< -o $@

packer: packer.o
	$(CC) $(CDEBUGFLAGS) -o $@ $^ $(LINKRAYLIB)

packer.o: packer.c
	$(CC) $(CFLAGS) $(CDEBUGFLAGS) -c $< -o $@

clean:
	rm -f main main_release packer *.o
