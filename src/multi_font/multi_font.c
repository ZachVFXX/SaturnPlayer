
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#include <stdlib.h>
#include <string.h>
#include "stdint.h"
#include "stdbool.h"
#include "raylib.h"

#define ATLAS_SIZE 4096
#define GLYPH_PAD  2

typedef struct {
    uint8_t* pixels;
    int      cursor_x;
    int      cursor_y;
    int      row_h;
} Atlas;

static bool atlas_pack(Atlas* a, int w, int h, int* ox, int* oy) {
    if (a->cursor_x + w + GLYPH_PAD > ATLAS_SIZE) {
        a->cursor_x = GLYPH_PAD;
        a->cursor_y += a->row_h + GLYPH_PAD;
        a->row_h    = 0;
    }
    if (a->cursor_y + h + GLYPH_PAD > ATLAS_SIZE) return false;
    *ox = a->cursor_x;
    *oy = a->cursor_y;
    a->cursor_x += w + GLYPH_PAD;
    if (h > a->row_h) a->row_h = h;
    return true;
}

static void atlas_blit(Atlas* a, uint8_t* src, int w, int h,
                        int pitch, int dx, int dy) {
    for (int row = 0; row < h; row++)
        memcpy(a->pixels + (dy + row) * ATLAS_SIZE + dx,
               src + row * pitch, w);
}

#define FACE_CACHE_MAX 64

typedef struct { char path[512]; FT_Face face; } FaceEntry;
typedef struct {
    FaceEntry  entries[FACE_CACHE_MAX];
    int        count;
    FT_Library ft;
    int        pixel_size;
} FaceCache;

static FT_Face face_cache_get_or_load(FaceCache* c, const char* path) {
    for (int i = 0; i < c->count; i++)
        if (strcmp(c->entries[i].path, path) == 0)
            return c->entries[i].face;

    if (c->count >= FACE_CACHE_MAX) {
        TraceLog(LOG_WARNING, "MULTIFONT: face cache full, skipping %s", path);
        return NULL;
    }

    FT_Face face;
    if (FT_New_Face(c->ft, path, 0, &face) != 0) return NULL;
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)c->pixel_size);

    strncpy(c->entries[c->count].path, path, 511);
    c->entries[c->count].path[511] = '\0';
    c->entries[c->count].face      = face;
    c->count++;

    TraceLog(LOG_INFO, "MULTIFONT: loaded face [%d] %s", c->count - 1, path);
    return face;
}

static void face_cache_destroy(FaceCache* c) {
    for (int i = 0; i < c->count; i++) FT_Done_Face(c->entries[i].face);
    c->count = 0;
}

Font BuildMultiFontAtlas(const char*  primary_family,
                          const char** preferred_families,
                          int          preferred_count,
                          int          font_size,
                          int*         codepoints,
                          int          codepoint_count) {
    Font result = {0};

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        TraceLog(LOG_ERROR, "MULTIFONT: FreeType init failed");
        return result;
    }

    FaceCache cache = { .ft = ft, .pixel_size = font_size };

    FcConfig* fc = FcInitLoadConfigAndFonts();
    if (!fc) {
        TraceLog(LOG_ERROR, "MULTIFONT: fontconfig init failed");
        FT_Done_FreeType(ft);
        return result;
    }

    // One FcFontSort for the entire atlas
    // Build a pattern with family priority list and no-color constraint.
    // FcFontSort returns ALL matching fonts ranked by score — we walk them
    // once and let each font claim the codepoints it covers.
    FcPattern* pat = FcPatternCreate();
    FcPatternAddBool(pat, FC_COLOR, FcFalse);

    // Primary first, then caller's preferred list
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)primary_family);
    for (int i = 0; i < preferred_count; i++)
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)preferred_families[i]);

    FcConfigSubstitute(fc, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult   res;
    FcFontSet* fset = FcFontSort(fc, pat, FcTrue, NULL, &res);
    FcPatternDestroy(pat);

    if (!fset) {
        TraceLog(LOG_ERROR, "MULTIFONT: FcFontSort returned nothing");
        FcConfigDestroy(fc);
        FT_Done_FreeType(ft);
        return result;
    }

    // Allocate glyph arrays
    GlyphInfo* glyphs  = calloc(codepoint_count, sizeof(GlyphInfo));
    Rectangle* recs    = calloc(codepoint_count, sizeof(Rectangle));
    bool*      covered = calloc(codepoint_count, sizeof(bool));

    Atlas atlas = {
        .pixels   = calloc(ATLAS_SIZE * ATLAS_SIZE, 1),
        .cursor_x = GLYPH_PAD,
        .cursor_y = GLYPH_PAD,
        .row_h    = 0,
    };

    // Safe defaults
    int fallback_advance = font_size / 2;
    for (int ci = 0; ci < codepoint_count; ci++) {
        glyphs[ci].value    = codepoints[ci];
        glyphs[ci].advanceX = fallback_advance;
        recs[ci]            = (Rectangle){ 0, 0, 0, 0 };
    }

    int rasterized = 0;
    int remaining  = codepoint_count;
    bool atlas_full = false;

    // Walk fonts in priority order
    for (int fi = 0; fi < fset->nfont && remaining > 0 && !atlas_full; fi++) {
        FcPattern* cpat = fset->fonts[fi];

        // Skip color fonts
        FcBool is_color = FcFalse;
        FcPatternGetBool(cpat, FC_COLOR, 0, &is_color);
        if (is_color) continue;

        FcChar8* path = NULL;
        if (FcPatternGetString(cpat, FC_FILE, 0, &path) != FcResultMatch)
            continue;

        FT_Face face = face_cache_get_or_load(&cache, (const char*)path);
        if (!face) continue;

        // Sweep all uncovered codepoints against this face
        for (int ci = 0; ci < codepoint_count && !atlas_full; ci++) {
            if (covered[ci]) continue;

            FT_UInt gi = FT_Get_Char_Index(face, (uint32_t)codepoints[ci]);
            if (gi == 0) continue;

            if (FT_Load_Glyph(face, gi, FT_LOAD_RENDER) != 0) continue;

            FT_GlyphSlot slot = face->glyph;
            FT_Bitmap*   bm   = &slot->bitmap;

            // Mark covered so no other font tries this codepoint again
            covered[ci] = true;
            remaining--;

            glyphs[ci].advanceX = (int)(slot->advance.x >> 6);
            glyphs[ci].offsetX  = slot->bitmap_left;
            glyphs[ci].offsetY  = font_size - slot->bitmap_top;

            int w = (int)bm->width;
            int h = (int)bm->rows;

            // Whitespace / invisible glyphs — metrics filled, no pixels
            if (w == 0 || h == 0) continue;

            // Skip non-grayscale (color bitmap fonts sneak through sometimes)
            if (bm->pixel_mode != FT_PIXEL_MODE_GRAY) continue;

            int px, py;
            if (!atlas_pack(&atlas, w, h, &px, &py)) {
                TraceLog(LOG_WARNING,
                         "MULTIFONT: atlas full at U+%04X after %d glyphs",
                         (uint32_t)codepoints[ci], rasterized);
                atlas_full = true;
                break;
            }

            atlas_blit(&atlas, bm->buffer, w, h, (int)bm->pitch, px, py);
            recs[ci] = (Rectangle){ (float)px, (float)py, (float)w, (float)h };
            rasterized++;
        }
    }

    int missing = 0;
    for (int ci = 0; ci < codepoint_count; ci++)
        if (!covered[ci]) missing++;

    TraceLog(LOG_INFO,
             "MULTIFONT: \"%s\" — %d rasterized | %d missing | %d faces loaded",
             primary_family, rasterized, missing, cache.count);

    // Grayscale to RGBA
    uint8_t* rgba = malloc((size_t)(ATLAS_SIZE * ATLAS_SIZE * 4));
    for (int i = 0; i < ATLAS_SIZE * ATLAS_SIZE; i++) {
        rgba[i*4+0] = 255;
        rgba[i*4+1] = 255;
        rgba[i*4+2] = 255;
        rgba[i*4+3] = atlas.pixels[i];
    }

    Image img = {
        .data    = rgba,
        .width   = ATLAS_SIZE,
        .height  = ATLAS_SIZE,
        .mipmaps = 1,
        .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };

    result.baseSize     = font_size;
    result.glyphCount   = codepoint_count;
    result.glyphPadding = GLYPH_PAD;
    result.texture      = LoadTextureFromImage(img);
    result.recs         = recs;
    result.glyphs       = glyphs;

    SetTextureFilter(result.texture, TEXTURE_FILTER_BILINEAR);

    // Cleanup
    free(rgba);
    free(atlas.pixels);
    free(covered);
    face_cache_destroy(&cache);
    FT_Done_FreeType(ft);
    FcFontSetDestroy(fset);
    FcConfigDestroy(fc);

    return result;
}


typedef struct { int* data; int count; int cap; } IntVec;

static void ivec_push(IntVec* v, int val) {
    if (v->count == v->cap) {
        v->cap  = v->cap ? v->cap * 2 : 256;
        v->data = realloc(v->data, v->cap * sizeof(int));
    }
    v->data[v->count++] = val;
}

static int cmp_int(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

int* CollectCodepoints(const char** strings, int string_count, int* out_count) {
    IntVec v = {0};

    for (int cp = 0x20; cp <= 0x7E; cp++) ivec_push(&v, cp);

    for (int si = 0; si < string_count; si++) {
        const char* s = strings[si];
        if (!s) continue;
        int i   = 0;
        int len = (int)strlen(s);
        while (i < len) {
            int cpSize = 0;
            int cp = GetCodepoint(s + i, &cpSize);
            if (cpSize == 0) { i++; continue; }
            if (cp > 0x1F) ivec_push(&v, cp);
            i += cpSize;
        }
    }

    qsort(v.data, v.count, sizeof(int), cmp_int);
    int unique = 0;
    for (int i = 0; i < v.count; i++)
        if (i == 0 || v.data[i] != v.data[i-1])
            v.data[unique++] = v.data[i];

    *out_count = unique;
    return v.data;
}
