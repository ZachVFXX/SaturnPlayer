.PHONY: clean clean_raylib clean_ffmpeg clean_freetype clean_harfbuzz clean_local
.PHONY: build_ffmpeg build_raylib build_freetype fetch_freetype build_harfbuzz
.PHONY: debug release debug_build release_build

# Platform detection  (can be overridden: make release PLATFORM=Windows)
PLATFORM ?= $(shell uname)

$(info Building for $(PLATFORM))

JOBS := 4

ifeq ($(PLATFORM), Linux)
	CC             ?= tcc
	PLATFORM_LIBS  = -lGL -lX11 -lm -lpthread -ldl -lrt -lz
	CDEBUGFLAGS    = -ggdb -g -fsanitize=address
	CRELEASEFLAGS  = -O3
	TARGET_OS      = linux
	BIN_SUFFIX     =
	GLFW_BACKEND   = -D_GLFW_X11
else ifeq ($(PLATFORM), Darwin)
	CC             ?= clang
	PLATFORM_LIBS  = -framework OpenGL -framework Cocoa     \
	                 -framework IOKit -framework CoreAudio \
	                 -framework CoreVideo \
	                 -lm -lpthread -ldl -lz
	CDEBUGFLAGS    = -ggdb -g -fsanitize=address
	CRELEASEFLAGS  = -O3
	TARGET_OS      = darwin
	BIN_SUFFIX     =
	GLFW_BACKEND   = -D_GLFW_COCOA
else
	# Windows with mingw-w64
	CC             ?= x86_64-w64-mingw32-gcc
	PLATFORM_LIBS  = -lopengl32 -lgdi32 -lwinmm -lws2_32 \
	                 -lpthread -lm -lbcrypt -ldwmapi -lole32
	CDEBUGFLAGS    = -ggdb -g
	CRELEASEFLAGS  = -O3 -mwindows
	TARGET_OS      = mingw32
	BIN_SUFFIX     = .exe
	GLFW_BACKEND   = -D_GLFW_WIN32
endif

# Use the actual compiler to get the correct target triplet
OS_TRIPLET = $(shell $(CC) -dumpmachine)

$(info Building using $(CC))

# Paths
EXT_PATH = external

# FFmpeg
FFMPEG_PATH  = $(EXT_PATH)/ffmpeg
FFMPEG_BUILD = $(FFMPEG_PATH)/build

# Raylib
RAYLIB_PATH  = $(EXT_PATH)/raylib/src

# HarfBuzz
HARFBUZZ_PATH  = $(EXT_PATH)/harfbuzz
HARFBUZZ_BUILD = $(HARFBUZZ_PATH)/meson-build

# FreeType
FREETYPE_PATH    = $(EXT_PATH)/freetype
FREETYPE_BUILD   = $(FREETYPE_PATH)/build
FREETYPE_VERSION = 2.14.1
FREETYPE_ARCHIVE = freetype-$(FREETYPE_VERSION).tar.xz
FREETYPE_URL     = https://sourceforge.net/projects/freetype/files/freetype2/$(FREETYPE_VERSION)/$(FREETYPE_ARCHIVE)/download

# Compiler flags
INCLUDES = -I$(RAYLIB_PATH)           \
           -I$(FFMPEG_PATH)           \
           -I$(FFMPEG_BUILD)          \
           -I$(FREETYPE_PATH)/include \
           -I$(HARFBUZZ_PATH)/include/harfbuzz \
           -I./src

LIBS = -L$(RAYLIB_PATH) -lraylib \
       $(FFMPEG_BUILD)/libavformat/libavformat.a \
       $(FFMPEG_BUILD)/libavcodec/libavcodec.a   \
       $(FFMPEG_BUILD)/libavutil/libavutil.a     \
       $(FFMPEG_BUILD)/libswresample/libswresample.a \
       $(FREETYPE_PATH)/objs/.libs/libfreetype.a \
       -L$(HARFBUZZ_PATH)/lib -lharfbuzz \
       $(PLATFORM_LIBS)

CFLAGS = -std=c99 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -DPLATFORM_DESKTOP

SRCS = src/core/song.c src/utils/vector.c src/main.c src/win/titlebar.c src/metadata/metadata.c src/utils/arena.c src/core/core.c
OBJS = $(SRCS:.c=.o)

# Top-level targets
debug_build:   build_ffmpeg build_freetype build_harfbuzz build_raylib saturn_debug$(BIN_SUFFIX)
release_build: build_ffmpeg build_freetype build_harfbuzz build_raylib saturn_player$(BIN_SUFFIX)

debug:   saturn_debug$(BIN_SUFFIX)
release: saturn_player$(BIN_SUFFIX)
	strip saturn_player$(BIN_SUFFIX)
# Compilation
%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(INCLUDES) $(EXTRA_FLAGS)

# Linking
saturn_debug$(BIN_SUFFIX): EXTRA_FLAGS = $(CDEBUGFLAGS)
saturn_player$(BIN_SUFFIX): EXTRA_FLAGS = $(CRELEASEFLAGS)

saturn_debug$(BIN_SUFFIX): $(OBJS)
	$(CC) -o $@ $^ $(LIBS) $(CDEBUGFLAGS)

saturn_player$(BIN_SUFFIX): $(OBJS)
	$(CC) -o $@ $^ $(LIBS) $(CRELEASEFLAGS)

# FFmpeg
build_ffmpeg:
	$(info Building ffmpeg)
	mkdir -p $(FFMPEG_BUILD)
	cd $(FFMPEG_BUILD) && \
	$(CURDIR)/$(FFMPEG_PATH)/configure \
		--target-os=$(TARGET_OS) \
		--arch=x86_64 \
		--cc=$(CC) \
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
		--disable-zlib \
		--disable-debug \
		--disable-doc && \
	$(MAKE) -j$(JOBS)
	$(info FFmpeg built successfully)

# Raylib
build_raylib:
	$(info Building raylib)
	$(MAKE) -j$(JOBS) -C $(RAYLIB_PATH)    \
		PLATFORM=PLATFORM_DESKTOP          \
		RAYLIB_BUILD_MODE=STATIC           \
		CC=$(CC)                           \
		OS=$(PLATFORM)                     \
		CFLAGS="-DSUPPORT_MODULE_RMODELS=0 \
		-DPLATFORM_DESKTOP_GLFW            \
		$(GLFW_BACKEND)                    \
		-DSUPPORT_CAMERA_SYSTEM=0          \
		-DSUPPORT_FILEFORMAT_JPG=1         \
		-DSUPPORT_IMAGE_EXPORT=0           \
		-DSUPPORT_IMAGE_GENERATION=0       \
		-DSUPPORT_CLIPBOARD_IMAGE=0        \
		-DSUPPORT_AUTOMATION_EVENTS=0      \
		-DSUPPORT_COMPRESSION_API=0        \
		-DSUPPORT_SCREEN_CAPTURE=0         \
		-DSUPPORT_RPRAND_GENERATOR=0       \
		-DSUPPORT_FILEFORMAT_FNT=0"
	$(info Raylib built successfully)

# FreeType
$(FREETYPE_ARCHIVE):
	curl -fL --retry 3 --output $@ $(FREETYPE_URL)

$(FREETYPE_PATH): $(FREETYPE_ARCHIVE)
	rm -rf $(FREETYPE_PATH)
	mkdir -p $(FREETYPE_PATH)
	tar -xf $(FREETYPE_ARCHIVE) --strip-components=1 -C $(FREETYPE_PATH)

fetch_freetype: $(FREETYPE_PATH)

build_freetype: fetch_freetype
	$(info Building freetype)
	mkdir -p $(FREETYPE_BUILD)
	cd $(FREETYPE_PATH) && \
	CC=$(CC) ./configure \
		--host=$(OS_TRIPLET) \
		--enable-static \
		--disable-shared \
		--without-brotli \
		--without-harfbuzz \
		--without-png \
		--without-bzip2 \
		--without-doc \
		--with-zlib=no && \
	$(MAKE) -j$(JOBS) CC=$(CC)
	$(info FreeType built successfully)

# HarfBuzz
build_harfbuzz: build_freetype
	$(info Building harfbuzz)
	mkdir -p $(CURDIR)/$(FREETYPE_BUILD)/pkgconfig
	printf 'prefix=%s\nexec_prefix=$${prefix}\nlibdir=%s\nincludedir=%s\n\nName: FreeType 2\nDescription: A free, high-quality font engine\nVersion: $(FREETYPE_VERSION)\nCflags: -I$${includedir}\nLibs: $${libdir}/libfreetype.a\n' \
		"$(CURDIR)/$(FREETYPE_PATH)" \
		"$(CURDIR)/$(FREETYPE_PATH)/objs/.libs" \
		"$(CURDIR)/$(FREETYPE_PATH)/include" \
		> $(CURDIR)/$(FREETYPE_BUILD)/pkgconfig/freetype2.pc
	CC=$(CC) PKG_CONFIG_PATH=$(CURDIR)/$(FREETYPE_BUILD)/pkgconfig \
	meson setup --reconfigure \
		--default-library=static \
		--prefix=$(CURDIR)/$(HARFBUZZ_PATH) \
		-Dtests=disabled \
		-Ddocs=disabled \
		-Dbenchmark=disabled \
		-Dintrospection=disabled \
		-Dfreetype=enabled \
		$(HARFBUZZ_BUILD) $(HARFBUZZ_PATH)
	meson compile -C $(HARFBUZZ_BUILD) -j$(JOBS)
	meson install -C $(HARFBUZZ_BUILD)
	$(info HarfBuzz built successfully)

# Clean
clean: clean_local clean_raylib clean_ffmpeg clean_freetype clean_harfbuzz

clean_local:
	rm -f src/*.o src/win/*.o src/core/*.o src/utils/*.o src/metadata/*.o saturn_debug saturn_player saturn_debug.exe saturn_player.exe

clean_raylib:
	@$(MAKE) -C $(RAYLIB_PATH) clean || true

clean_ffmpeg:
	@$(MAKE) -C $(FFMPEG_BUILD) distclean 2>/dev/null || rm -rf $(FFMPEG_BUILD)

clean_freetype:
	@$(MAKE) -C $(FREETYPE_PATH) clean 2>/dev/null || true

clean_harfbuzz:
	rm -rf $(HARFBUZZ_BUILD)
