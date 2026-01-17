#include <id3v2lib-2.0/id3v2lib.h>
#include <iconv.h>
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

char* get_displayable_text(mem_arena* arena, ID3v2_TextFrame* f)
{
    if (!f || !f->data || !f->data->text)
        return NULL;

    /* UTF-8 - no conversion needed */
    if (f->data->encoding == 0x03)
        return arena_strdup(arena, f->data->text);

    const char* from_encoding = NULL;
    char* inbuf = f->data->text;
    size_t inbytesleft = f->header->size - 1; // Subtract encoding byte

    /* ISO-8859-1 (Latin-1) */
    if (f->data->encoding == 0x00)
    {
        from_encoding = "ISO-8859-1";
    }
    /* UTF-16 with BOM */
    else if (f->data->encoding == 0x01)
    {
        // Detect and skip BOM
        if (inbytesleft >= 2)
        {
            unsigned char b1 = (unsigned char)inbuf[0];
            unsigned char b2 = (unsigned char)inbuf[1];

            if (b1 == 0xFF && b2 == 0xFE)
            {
                from_encoding = "UTF-16LE";
                inbuf += 2;
                inbytesleft -= 2;
            }
            else if (b1 == 0xFE && b2 == 0xFF)
            {
                from_encoding = "UTF-16BE";
                inbuf += 2;
                inbytesleft -= 2;
            }
            else
            {
                // No BOM, assume UTF-16LE
                from_encoding = "UTF-16LE";
            }
        }
        else
        {
            return NULL;
        }

        // Find UTF-16 null terminator
        size_t actual_len = 0;
        for (size_t i = 0; i < inbytesleft - 1; i += 2)
        {
            if (inbuf[i] == 0 && inbuf[i + 1] == 0)
            {
                actual_len = i;
                break;
            }
        }
        if (actual_len > 0)
            inbytesleft = actual_len;
    }
    /* UTF-16BE without BOM */
    else if (f->data->encoding == 0x02)
    {
        from_encoding = "UTF-16BE";

        // Find UTF-16 null terminator
        size_t actual_len = 0;
        for (size_t i = 0; i < inbytesleft - 1; i += 2)
        {
            if (inbuf[i] == 0 && inbuf[i + 1] == 0)
            {
                actual_len = i;
                break;
            }
        }
        if (actual_len > 0)
            inbytesleft = actual_len;
    }
    else
    {
        return NULL;
    }

    // For ISO-8859-1, find null terminator
    if (f->data->encoding == 0x00)
    {
        size_t actual_len = 0;
        for (size_t i = 0; i < inbytesleft; i++)
        {
            if (inbuf[i] == 0)
            {
                actual_len = i;
                break;
            }
        }
        if (actual_len > 0)
            inbytesleft = actual_len;
    }

    iconv_t cd = iconv_open("UTF-8", from_encoding);
    if (cd == (iconv_t)-1)
    {
        printf("iconv_open failed");
        return NULL;
    }

    mem_arena* scratch = arena_create(MiB(1), KiB(1));

    // Allocate output buffer (generous size for UTF-8)
    size_t out_capacity = inbytesleft * 4 + 1;
    char* scratch_out = arena_push(scratch, out_capacity, false);
    char* outbuf = scratch_out;
    size_t outbytesleft = out_capacity;

    size_t result = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    iconv_close(cd);

    if (result == (size_t)-1)
    {
        perror("iconv conversion failed");
        arena_destroy(scratch);
        return NULL;
    }

    *outbuf = '\0';
    char* final = arena_strdup(arena, scratch_out);
    arena_destroy(scratch);

    return final;
}

const char* get_file_extension_from_mime(const char* mime_type)
{
    if (mime_type == NULL) return ".bin";
    if (strcmp(mime_type, "image/jpeg") == 0) return ".jpg";
    if (strcmp(mime_type, "image/png") == 0) return ".png";
    if (strcmp(mime_type, "image/gif") == 0) return ".gif";
    return ".jpg";
}


Metadata* get_metadata_from_mp3(mem_arena *arena, char* filepath)
{
    ImageBuffer* img = PUSH_STRUCT(arena, ImageBuffer);
    Metadata* metadata = PUSH_STRUCT(arena, Metadata);

    ID3v2_Tag* tag = ID3v2_read_tag(filepath);

    if (!tag) {
        fprintf(stderr, "Failed to read tag from %s\n", filepath);
        tag = ID3v2_Tag_new_empty();
    }

    metadata->title_text = get_displayable_text(arena, ID3v2_Tag_get_title_frame(tag));
    metadata->artist_text = get_displayable_text(arena, ID3v2_Tag_get_artist_frame(tag));
    metadata->album_text = get_displayable_text(arena, ID3v2_Tag_get_album_frame(tag));

    ID3v2_ApicFrame* apic_frame = ID3v2_Tag_get_album_cover_frame(tag);
    if (apic_frame && apic_frame->data && apic_frame->data->data)
    {
        img->size = apic_frame->data->picture_size;
        img->data = arena_push(arena, img->size, false);
        img->mime_type = arena_push(arena, ID3v2_strlent(apic_frame->data->mime_type), false);

        if (!img->data) {
            fprintf(stderr, "Memory allocation failed for cover buffer\n");
        } else {
            memcpy(img->data, apic_frame->data->data, img->size);
            memcpy(img->mime_type, apic_frame->data->mime_type, ID3v2_strlent(apic_frame->data->mime_type));
            printf("Cover loaded in memory (%zu bytes, %s)\n",
                   img->size,
                   img->mime_type);
        }
    }
    else {
        printf("No cover found\n");
    }

    metadata->image = img;
    ID3v2_Tag_free(tag);
    return metadata;
}
