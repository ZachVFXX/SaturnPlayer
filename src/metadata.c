#include <id3tag.h>
#include <raylib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

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

char* get_text_from_field(mem_arena* arena, union id3_field const* field)
{
    if (!field)
        return NULL;

    id3_ucs4_t const* ucs4 = id3_field_getstrings(field, 0);
    if (!ucs4)
        return NULL;

    // Convert UCS-4 to UTF-8
    id3_utf8_t* utf8 = id3_ucs4_utf8duplicate(ucs4);
    if (!utf8)
        return NULL;

    char* result = arena_strdup(arena, (char*)utf8);
    free(utf8);

    return result;
}

// Get text frame by frame ID
char* get_text_frame(mem_arena* arena, struct id3_tag* tag, char const* frame_id)
{
    if (!tag || !frame_id)
        return NULL;

    struct id3_frame* frame = id3_tag_findframe(tag, frame_id, 0);
    if (!frame)
        return NULL;

    // Text frames have the string list in field 1 (field 0 is text encoding)
    union id3_field* field = id3_frame_field(frame, 1);
    if (!field)
        return NULL;

    return get_text_from_field(arena, field);
}

// Extract APIC (album cover) frame
ImageBuffer* get_album_cover(mem_arena* arena, struct id3_tag* tag)
{
    struct id3_frame* frame = id3_tag_findframe(tag, "APIC", 0);
    if (!frame)
        return NULL;

    ImageBuffer* img = PUSH_STRUCT(arena, ImageBuffer);

    // APIC frame structure:
    // Field 0: Text encoding
    // Field 1: MIME type
    // Field 2: Picture type
    // Field 3: Description
    // Field 4: Picture data

    // Get MIME type
    union id3_field* mime_field = id3_frame_field(frame, 1);
    if (mime_field) {
        id3_latin1_t const* mime = id3_field_getlatin1(mime_field);
        if (mime) {
            size_t mime_len = strlen((char*)mime) + 1;
            img->mime_type = arena_push(arena, mime_len, false);
            memcpy(img->mime_type, mime, mime_len);
        }
    }
    // Get picture data
    union id3_field* data_field = id3_frame_field(frame, 4);
    if (!data_field)
        return NULL;

    id3_byte_t const* data;
    id3_length_t length;

    data = id3_field_getbinarydata(data_field, &length);
    if (!data || length == 0)
        return NULL;

    img->size = length;
    img->data = arena_push(arena, img->size, false);
    memcpy(img->data, data, img->size);

    TraceLog(LOG_INFO, "METADATA: Cover loaded in memory (%zu bytes, %s).",
             img->size, img->mime_type ? img->mime_type : "unknown");

    return img;
}

const char* get_file_extension_from_mime(const char* mime_type)
{
    if (mime_type == NULL) return ".bin";
    if (strcmp(mime_type, "image/jpeg") == 0) return ".jpg";
    if (strcmp(mime_type, "image/png") == 0) return ".png";
    if (strcmp(mime_type, "image/gif") == 0) return ".gif";
    return ".jpg";
}

Metadata* get_metadata_from_mp3(mem_arena* arena, char* filepath)
{
    Metadata* metadata = PUSH_STRUCT(arena, Metadata);

    struct id3_file* file = id3_file_open(filepath, ID3_FILE_MODE_READONLY);
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return metadata;
    }

    struct id3_tag* tag = id3_file_tag(file);
    if (!tag) {
        fprintf(stderr, "No ID3 tag found in %s\n", filepath);
        id3_file_close(file);
        return metadata;
    }

    metadata->title_text = get_text_frame(arena, tag, ID3_FRAME_TITLE);
    metadata->artist_text = get_text_frame(arena, tag, ID3_FRAME_ARTIST);
    metadata->album_text = get_text_frame(arena, tag, ID3_FRAME_ALBUM);

    metadata->image = get_album_cover(arena, tag);
    if (!metadata->image) {
        TraceLog(LOG_WARNING, "METADATA: No cover found for %s.", filepath);
    }

    id3_file_close(file);
    return metadata;
}
