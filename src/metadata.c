#include <ffmpeg/libavformat/avformat.h>
#include <raylib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define ARENA_IMPLEMENTATION
#include "utils/arena.h"

typedef struct {
    unsigned char* data;
    size_t size;
    char* mime_type;
} ImageBuffer;

typedef struct {
    char* title_text;
    char* artist_text;
    char* album_text;
    ImageBuffer* image;
} Metadata;

const char* get_file_extension_from_mime(const char* mime_type)
{
    if (mime_type == NULL) return ".bin";
    if (strcmp(mime_type, "image/jpeg") == 0) return ".jpg";
    if (strcmp(mime_type, "image/png") == 0) return ".png";
    if (strcmp(mime_type, "image/gif") == 0) return ".gif";
    return ".jpg";
}

char* get_metadata_string(mem_arena* arena, AVDictionary* metadata, const char* key)
{
    if (!metadata || !key)
        return NULL;

    AVDictionaryEntry* entry = av_dict_get(metadata, key, NULL, 0);
    if (!entry || !entry->value)
        return NULL;

    return arena_strdup(arena, entry->value);
}

const char* get_mime_from_codec(enum AVCodecID codec_id)
{
    switch (codec_id) {
        case AV_CODEC_ID_MJPEG:
        case AV_CODEC_ID_JPEGLS:
            return "image/jpeg";
        case AV_CODEC_ID_PNG:
            return "image/png";
        case AV_CODEC_ID_GIF:
            return "image/gif";
        case AV_CODEC_ID_BMP:
            return "image/bmp";
        default:
            return "image/jpeg";
    }
}

ImageBuffer* get_album_cover(mem_arena* arena, AVFormatContext* fmt_ctx)
{
    if (!fmt_ctx)
        return NULL;

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream* stream = fmt_ctx->streams[i];

        if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            AVPacket* pkt = &stream->attached_pic;

            if (!pkt->data || pkt->size <= 0)
                continue;

            ImageBuffer* img = PUSH_STRUCT(arena, ImageBuffer);

            img->size = pkt->size;
            img->data = arena_push(arena, img->size, false);
            memcpy(img->data, pkt->data, img->size);

            const char* mime = get_mime_from_codec(stream->codecpar->codec_id);
            img->mime_type = arena_strdup(arena, mime);

            TraceLog(LOG_INFO, "METADATA: Cover loaded in memory (%zu bytes, %s).",
                     img->size, img->mime_type);

            return img;
        }
    }

    return NULL;
}

Metadata* get_metadata_from_mp3(mem_arena* arena, char* filepath)
{
    Metadata* metadata = PUSH_STRUCT(arena, Metadata);
    AVFormatContext* fmt_ctx = NULL;

    if (avformat_open_input(&fmt_ctx, filepath, NULL, NULL) < 0) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return metadata;
    }

    metadata->title_text = get_metadata_string(arena, fmt_ctx->metadata, "title");
    metadata->artist_text = get_metadata_string(arena, fmt_ctx->metadata, "artist");
    metadata->album_text = get_metadata_string(arena, fmt_ctx->metadata, "album");

    /* Extract album cover */
    metadata->image = get_album_cover(arena, fmt_ctx);
    if (!metadata->image) {
        TraceLog(LOG_WARNING, "METADATA: No cover found for %s.", filepath);
    }

    avformat_close_input(&fmt_ctx);

    return metadata;
}
