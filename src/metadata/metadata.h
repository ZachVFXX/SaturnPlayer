#ifndef METADATA_H
#define METADATA_H
#include "../../external/raylib/src/raylib.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <libavformat/avformat.h>
#include "../utils/arena.h"


typedef struct {
    unsigned char* data;
    size_t size;
    char* mime_type;
} ImageMetadata;

typedef struct {
    char* title_text;
    char* artist_text;
    char* album_text;
    ImageMetadata* image;
} Metadata;

const char* get_file_extension_from_mime(const char* mime_type);

char* get_metadata_string(mem_arena* arena, AVDictionary* metadata, const char* key);

const char* get_mime_from_codec(enum AVCodecID codec_id);

ImageMetadata* get_album_cover(mem_arena* arena, AVFormatContext* fmt_ctx);

Metadata* get_metadata_from_mp3(mem_arena* arena, char* filepath);
#endif
