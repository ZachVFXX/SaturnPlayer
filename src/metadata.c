#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iconv.h>
#include <id3v2lib-2.0/id3v2lib.h>
#define ARENA_IMPLEMENTATION
#include "arena.h"

typedef struct {
    unsigned char* data;
    size_t size;
    const char* mime_type;
} ImageBuffer;

typedef struct {
    char* title_text;
    char* artist_text;
    char* album_text;
    ImageBuffer* image;
} Metadata;

// This function will convert the text to a displayable format.
// The caller is responsible for freeing the returned string.
char* get_displayable_text(mem_arena* arena, ID3v2_TextFrame* f)
{
    if (!f || !f->data || !f->data->text)
        return arena_strdup(arena, "<none>");

    /* ISO-8859-1 ou UTF-8 */
    if (f->data->encoding == 0x00 || f->data->encoding == 0x03)
        return arena_strdup(arena, f->data->text);

    const char* from_encoding = NULL;
    if (f->data->encoding == 0x01)
        from_encoding = "UTF-16";
    else if (f->data->encoding == 0x02)
        from_encoding = "UTF-16BE";
    else
        return arena_strdup(arena, "<unsupported encoding>");

    iconv_t cd = iconv_open("UTF-8", from_encoding);
    if (cd == (iconv_t)-1)
        return arena_strdup(arena, "<iconv_open failed>");

    mem_arena* scratch = arena_create(MiB(1), KiB(1));

    char* inbuf = f->data->text;
    size_t inbytesleft = f->header->size - 1;

    size_t out_capacity = inbytesleft * 3 + 1;
    char* scratch_out = arena_push(scratch, out_capacity, false);

    char* outbuf = scratch_out;
    size_t outbytesleft = out_capacity;

    size_t result = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    iconv_close(cd);

    if (result == (size_t)-1)
    {
        arena_destroy(scratch);
        return arena_strdup(arena, "<iconv conversion failed>");
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
        img->mime_type = apic_frame->data->mime_type;

        if (!img->data) {
            fprintf(stderr, "Memory allocation failed for cover buffer\n");
        } else {
            memcpy(img->data, apic_frame->data->data, img->size);
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
