.PHONY: clean clean_raylib clean_ffmpeg clean_freetype

# Get the platform: Windows/Linux
PLATFORM:=$(shell uname)

$(info Building for $(PLATFORM))

JOBS := 4

ifeq ($(PLATFORM), Linux)
	CC = gcc
	PLATFORM_LIBS = -lGL -lX11 -lm -lpthread -ldl -lrt -lz
	CDEBUGFLAGS = -ggdb -g -fsanitize=address
	CRELEASEFLAGS = -O3
	TARGET_OS = linux
else
	CC = x86_64-w64-mingw32-gcc
	PLATFORM_LIBS = -lopengl32 -lgdi32 -lwinmm -lws2_32 -lpthread -lm -lbcrypt -ldwmapi -lole32
	CDEBUGFLAGS = -ggdb -g
	CRELEASEFLAGS = -O3 -mwindows
	TARGET_OS = mingw32
endif

OS_TRIPLET = $(shell gcc -dumpmachine)

$(info Building using $(CC))

EXT_PATH = external

FFMPEG_PATH   = $(EXT_PATH)/ffmpeg
FFMPEG_BUILD  = $(FFMPEG_PATH)/build

build_ffmpeg:
	$(info Building ffmpeg)
	mkdir -p $(FFMPEG_BUILD)
	cd $(FFMPEG_BUILD) && \
	$(CURDIR)/$(FFMPEG_PATH)/configure \
		--target-os=$(TARGET_OS) \
		--arch=x86_64 \
		--enable-static \
		--disable-shared \
		--disable-all \
		--disable-everything \
		--enable-small \
		--enable-avformat \
		--enable-avcodec \
		--enable-avutil \
		--enable-swresample \
		--enable-protocol=file \
		--enable-demuxer=mp3,mov,flac,ogg \
		--enable-decoder=mp3,aac,flac,vorbis,opus \
		--enable-parser=mpegaudio,flac,aac \
		--disable-vaapi \
		--disable-vdpau \
		--disable-vulkan \
		--disable-libdrm \
		--disable-network \
		--disable-debug \
		--disable-doc && \
	$(MAKE) -j$(JOBS)
	$(info FFmpeg build successfully)


RAYLIB_PATH   = $(EXT_PATH)/raylib/src
build_raylib:
	$(info Building raylib)
	$(MAKE) -j$(JOBS) -C $(RAYLIB_PATH) PLATFORM=PLATFORM_DESKTOP \
        RAYLIB_BUILD_MODE=STATIC           \
        CC=$(CC) \
        OS=$(PLATFORM) \
        CFLAGS="-DSUPPORT_MODULE_RMODELS=0\
        -DPLATFORM_DESKTOP_GLFW \
        -DSUPPORT_CAMERA_SYSTEM=0 \
        -DSUPPORT_FILEFORMAT_JPG=1 \
        -DSUPPORT_IMAGE_EXPORT=0\
        -DSUPPORT_IMAGE_GENERATION=0\
        -DSUPPORT_CLIPBOARD_IMAGE=0\
        -DSUPPORT_AUTOMATION_EVENTS=0\
        -DSUPPORT_COMPRESSION_API=0\
        -DSUPPORT_SCREEN_CAPTURE=0\
        -DSUPPORT_RPRAND_GENERATOR=0\
        -DSUPPORT_FILEFORMAT_FNT=0"\
   	$(info Raylib build successfully)

FREETYPE_PATH  = $(EXT_PATH)/freetype
FREETYPE_BUILD = $(FREETYPE_PATH)/build
TARGET_TRIPLET = x86_64-w64-mingw32

FREETYPE_VERSION = 2.13.2
FREETYPE_ARCHIVE = freetype-$(FREETYPE_VERSION).tar.xz
FREETYPE_URL = https://download.savannah.gnu.org/releases/freetype/$(FREETYPE_ARCHIVE)

EXT_PATH = external

$(FREETYPE_ARCHIVE):
	curl -fL --retry 3 --output $@ $(FREETYPE_URL)

$(FREETYPE_PATH): $(FREETYPE_ARCHIVE)
	rm -rf $(FREETYPE_PATH)
	mkdir -p $(FREETYPE_PATH)
	tar -xf $(FREETYPE_ARCHIVE) --strip-components=1 -C $(FREETYPE_PATH)

.PHONY: fetch_freetype
fetch_freetype: $(FREETYPE_PATH)

build_freetype: fetch_freetype
	$(info Building freetype)
	mkdir -p $(FREETYPE_BUILD)
	cd $(FREETYPE_PATH) && \
	./configure \
        --host=$(OS_TRIPLET) \
        --enable-static \
        --disable-shared \
        --without-brotli \
        --without-harfbuzz \
        --without-png \
        --without-bzip2 \
        --without-doc \
        --with-zlib=no && \
    $(MAKE) -j$(JOBS)
	$(info freetype build successfully)

INCLUDES = -I$(RAYLIB_PATH) \
           -I$(FFMPEG_PATH)/include \
           -I$(FREETYPE_PATH)/include \
           -I./src

LIBS = -L$(RAYLIB_PATH) -lraylib \
       $(FFMPEG_BUILD)/libavformat/libavformat.a \
       $(FFMPEG_BUILD)/libavcodec/libavcodec.a \
       $(FFMPEG_BUILD)/libavutil/libavutil.a \
       $(FFMPEG_BUILD)/libswresample/libswresample.a \
       $(FREETYPE_PATH)/objs/.libs/libfreetype.a \
       $(PLATFORM_LIBS)

SRCS = src/main.c
OBJS = src/main.o src/win/titlebar.o

CFLAGS = -std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -DPLATFORM_DESKTOP

debug: build_ffmpeg build_freetype build_raylib main.exe

release: build_ffmpeg build_freetype build_raylib main_release.exe

main.exe: $(OBJS)
	$(CC) -o $@ $^ $(LIBS) $(CDEBUGFLAGS)

main_release.exe: $(OBJS)
	$(CC) -o $@ $^ $(LIBS) $(CRELEASEFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(INCLUDES) $(CDEBUGFLAGS)

clean: clean_local clean_raylib clean_ffmpeg clean_freetype

clean_local:
	rm -f *.o *.exe

clean_raylib:
	@$(MAKE) -C $(RAYLIB_PATH) clean || true

clean_ffmpeg:
	@$(MAKE) -C $(FFMPEG_PATH) distclean || true

clean_freetype:
	@$(MAKE) -C $(FREETYPE_PATH) clean || true
