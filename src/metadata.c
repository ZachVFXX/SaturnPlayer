#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iconv.h>
#include <id3v2lib-2.0/id3v2lib.h>

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
char* get_displayable_text(ID3v2_TextFrame* f) {
    if (!f || !f->data || !f->data->text)
        return strdup("<none>");

    if (f->data->encoding == 0x00 || f->data->encoding == 0x03) // ISO-8859-1 or UTF-8
        return strdup(f->data->text);

    const char* from_encoding = NULL;
    if (f->data->encoding == 0x01) {
        from_encoding = "UTF-16"; // Let iconv detect endianness from BOM
    } else if (f->data->encoding == 0x02) {
        from_encoding = "UTF-16BE"; // Big-Endian without BOM
    } else {
        return strdup("<unsupported encoding>");
    }

    iconv_t cd = iconv_open("UTF-8", from_encoding);
    if (cd == (iconv_t)-1) {
        return strdup("<iconv_open failed>");
    }

    char* inbuf = f->data->text;
    size_t inbytesleft = f->header->size - 1; // Size of text data
    // A UTF-8 string can be up to 4x the size of a UTF-16 string in bytes in worst case scenarios, plus null terminator.
    size_t outbufsize = inbytesleft * 2 + 1;
    char* outbuf_start = malloc(outbufsize);
    if (!outbuf_start) {
        iconv_close(cd);
        return strdup("<malloc failed>");
    }
    char* outbuf = outbuf_start;
    size_t outbytesleft = outbufsize;

    size_t result = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    iconv_close(cd);

    if (result == (size_t)-1) {
        free(outbuf_start);
        return strdup("<iconv conversion failed>");
    }

    *outbuf = '\0'; // Null-terminate

    return outbuf_start;
}

const char* get_file_extension_from_mime(const char* mime_type)
{
    if (mime_type == NULL) return ".bin";
    if (strcmp(mime_type, "image/jpeg") == 0) return ".jpg";
    if (strcmp(mime_type, "image/png") == 0) return ".png";
    if (strcmp(mime_type, "image/gif") == 0) return ".gif";
    return ".bin";
}

// Source - https://stackoverflow.com/a
// Posted by Pavel Šimerda, modified by community. See post 'Timeline' for change history
// Retrieved 2026-01-14, License - CC BY-SA 4.0

void *memdup(const void *src, size_t n)
{
    void *dest;

    dest = malloc(n);
    if (dest == NULL)
            return NULL;

    return memcpy(dest, src, n);
}


Metadata* get_metadata_from_mp3(char* filepath)
{
    ImageBuffer* img = malloc(sizeof(ImageBuffer));
    Metadata* metadata = malloc(sizeof(Metadata));

    ID3v2_Tag* tag = ID3v2_read_tag(filepath);

    if (!tag) {
        fprintf(stderr, "Failed to read tag from %s\n", filepath);
    }

    metadata->title_text = get_displayable_text(ID3v2_Tag_get_title_frame(tag));
    metadata->artist_text = get_displayable_text(ID3v2_Tag_get_artist_frame(tag));
    metadata->album_text = get_displayable_text(ID3v2_Tag_get_album_frame(tag));

    ID3v2_ApicFrame* apic_frame = ID3v2_Tag_get_album_cover_frame(tag);
    if (apic_frame && apic_frame->data && apic_frame->data->data)
    {
        img->size = apic_frame->data->picture_size;
        img->data = malloc(img->size);
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
    //ID3v2_Tag_free(tag); // FIXME: NO FREE BECAUSE IT COREDUMP
    return metadata;
}

    /*
    ID3v2_ApicFrame* apic_frame = ID3v2_Tag_get_album_cover_frame(tag);
    if (apic_frame) {
        const char* ext = get_file_extension_from_mime(apic_frame->data->mime_type);
        char filename[20];
        snprintf(filename, sizeof(filename), "cover%s", ext);

        FILE* cover_file = fopen(filename, "wb");
        if (cover_file) {
            int picture_size = apic_frame->header->size;
            picture_size -= 1; // encoding
            picture_size -= strlen(apic_frame->data->mime_type) + 1;
            picture_size -= 1; // picture type
            picture_size -= strlen(apic_frame->data->description) + 1;

            fwrite(apic_frame->data->data, 1, picture_size, cover_file);
            fclose(cover_file);
            printf("Cover : Saved to %s\n", filename);
        } else {
            fprintf(stderr, "Could not open file to write cover art.\n");
        }
    }
    else {
        printf("Cover : <none>\n");
    }
    */
