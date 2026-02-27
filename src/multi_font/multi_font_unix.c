#include "../../external/freetype/include/ft2build.h"
#include "../../external/freetype/include/freetype/freetype.h"


#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../../raylib/src/raylib.h"

#define ATLAS_SIZE     4096
#define GLYPH_PAD      2
#define FACE_CACHE_MAX 64
#define MAX_FONT_FILES 2048
#define MAX_PATH_LEN   512

// ── Atlas ─────────────────────────────────────────────────────────────────────
typedef struct {
    uint8_t* pixels;
    int      cursor_x, cursor_y, row_h;
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

// ── Face cache ────────────────────────────────────────────────────────────────
typedef struct { char path[MAX_PATH_LEN]; FT_Face face; } FaceEntry;
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
    if (c->count >= FACE_CACHE_MAX) return NULL;
    FT_Face face;
    if (FT_New_Face(c->ft, path, 0, &face) != 0) return NULL;
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)c->pixel_size);
    strncpy(c->entries[c->count].path, path, MAX_PATH_LEN - 1);
    c->entries[c->count].face = face;
    c->count++;
    TraceLog(LOG_INFO, "MULTIFONT: loaded face [%d] %s", c->count - 1, path);
    return face;
}

static void face_cache_destroy(FaceCache* c) {
    for (int i = 0; i < c->count; i++) FT_Done_Face(c->entries[i].face);
    c->count = 0;
}

// ── Font discovery (no fontconfig) ───────────────────────────────────────────
typedef struct {
    char paths[MAX_FONT_FILES][MAX_PATH_LEN];
    int  count;
    int  named_count;
} FontList;

static void fontlist_push(FontList* fl, const char* path) {
    if (fl->count >= MAX_FONT_FILES) return;
    for (int i = 0; i < fl->count; i++)
        if (strcmp(fl->paths[i], path) == 0) return;
    snprintf(fl->paths[fl->count], MAX_PATH_LEN, "%s", path);
    fl->count++;
}

static bool has_font_ext(const char* name) {
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    return strcmp(dot, ".ttf") == 0 ||
           strcmp(dot, ".otf") == 0 ||
           strcmp(dot, ".ttc") == 0 ||
           strcmp(dot, ".TTF") == 0 ||
           strcmp(dot, ".OTF") == 0;
}

// Recursively scan a directory for font files
static void scan_dir(FontList* fl, const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[MAX_PATH_LEN];
        snprintf(full, MAX_PATH_LEN, "%s/%s", dir, e->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(fl, full);          // recurse
        } else if (S_ISREG(st.st_mode) && has_font_ext(e->d_name)) {
            fontlist_push(fl, full);
        }
    }
    closedir(d);
}

// Check if a font file likely matches a family name (filename heuristic)
static bool filename_matches_family(const char* path, const char* family) {
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;

    // Case-insensitive substring: "NotoSans" matches family "Noto Sans"
    // Compare by stripping spaces from family and doing substring search
    char fam_nospace[256] = {0};
    int  j = 0;
    for (int i = 0; family[i] && j < 255; i++)
        if (family[i] != ' ') fam_nospace[j++] = family[i];

    // Case-insensitive search
    char base_lower[MAX_PATH_LEN], fam_lower[256];
    strncpy(base_lower, base, MAX_PATH_LEN - 1);
    snprintf(fam_lower, sizeof(fam_lower), "%s", fam_nospace);
    for (char* p = base_lower; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    for (char* p = fam_lower;  *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;

    return strstr(base_lower, fam_lower) != NULL;
}

// Standard Linux font directories
static const char* FONT_DIRS[] = {
    "/usr/share/fonts",
    "/usr/local/share/fonts",
    "/usr/share/X11/fonts",
};
#define FONT_DIRS_COUNT 3

static FontList* build_font_list(const char*  primary_family,
                                  const char** preferred_families,
                                  int          preferred_count) {
    FontList* fl = calloc(1, sizeof(FontList));

    // First pass: collect ALL font files from all dirs into a temporary list
    FontList* all = calloc(1, sizeof(FontList));

    // Also check ~/.local/share/fonts and ~/.fonts
    const char* home = getenv("HOME");
    char user_fonts1[MAX_PATH_LEN], user_fonts2[MAX_PATH_LEN];
    if (home) {
        snprintf(user_fonts1, MAX_PATH_LEN, "%s/.local/share/fonts", home);
        snprintf(user_fonts2, MAX_PATH_LEN, "%s/.fonts", home);
        scan_dir(all, user_fonts1);
        scan_dir(all, user_fonts2);
    }
    for (int i = 0; i < FONT_DIRS_COUNT; i++)
        scan_dir(all, FONT_DIRS[i]);

    // Second pass: push named families first (priority order)
    const char* named[1 + 16];
    int named_count = 0;
    named[named_count++] = primary_family;
    for (int i = 0; i < preferred_count; i++)
        named[named_count++] = preferred_families[i];

    for (int ni = 0; ni < named_count; ni++)
        for (int fi = 0; fi < all->count; fi++)
            if (filename_matches_family(all->paths[fi], named[ni]))
                fontlist_push(fl, all->paths[fi]);

    fl->named_count = fl->count;

    // Third pass: everything else as fallback
    for (int fi = 0; fi < all->count; fi++)
        fontlist_push(fl, all->paths[fi]);

    free(all);
    TraceLog(LOG_INFO, "MULTIFONT: font list — %d named, %d total",
             fl->named_count, fl->count);
    return fl;
}

// ── BuildMultiFontAtlas (même signature publique) ─────────────────────────────
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
    FontList* fl    = build_font_list(primary_family, preferred_families, preferred_count);

    if (fl->count == 0) {
        TraceLog(LOG_ERROR, "MULTIFONT: no fonts found");
        free(fl);
        FT_Done_FreeType(ft);
        return result;
    }

    GlyphInfo* glyphs  = calloc(codepoint_count, sizeof(GlyphInfo));
    Rectangle* recs    = calloc(codepoint_count, sizeof(Rectangle));
    bool*      covered = calloc(codepoint_count, sizeof(bool));

    Atlas atlas = {
        .pixels   = calloc(ATLAS_SIZE * ATLAS_SIZE, 1),
        .cursor_x = GLYPH_PAD,
        .cursor_y = GLYPH_PAD,
    };

    int fallback_advance = font_size / 2;
    for (int ci = 0; ci < codepoint_count; ci++) {
        glyphs[ci].value    = codepoints[ci];
        glyphs[ci].advanceX = fallback_advance;
        recs[ci]            = (Rectangle){ 0, 0, 0, 0 };
    }

    int  rasterized = 0, remaining = codepoint_count;
    bool atlas_full = false;

    for (int fi = 0; fi < fl->count && remaining > 0 && !atlas_full; fi++) {
        FT_Face face = face_cache_get_or_load(&cache, fl->paths[fi]);
        if (!face) continue;

        for (int ci = 0; ci < codepoint_count && !atlas_full; ci++) {
            if (covered[ci]) continue;
            FT_UInt gi = FT_Get_Char_Index(face, (uint32_t)codepoints[ci]);
            if (gi == 0) continue;
            if (FT_Load_Glyph(face, gi, FT_LOAD_RENDER) != 0) continue;

            FT_GlyphSlot slot = face->glyph;
            FT_Bitmap*   bm   = &slot->bitmap;

            covered[ci] = true;
            remaining--;
            glyphs[ci].advanceX = (int)(slot->advance.x >> 6);
            glyphs[ci].offsetX  = slot->bitmap_left;
            glyphs[ci].offsetY  = font_size - slot->bitmap_top;

            int w = (int)bm->width, h = (int)bm->rows;
            if (w == 0 || h == 0) continue;
            if (bm->pixel_mode != FT_PIXEL_MODE_GRAY) continue;

            int px, py;
            if (!atlas_pack(&atlas, w, h, &px, &py)) {
                TraceLog(LOG_WARNING, "MULTIFONT: atlas full after %d glyphs", rasterized);
                atlas_full = true;
                break;
            }
            atlas_blit(&atlas, bm->buffer, w, h, (int)bm->pitch, px, py);
            recs[ci] = (Rectangle){ (float)px, (float)py, (float)w, (float)h };
            rasterized++;
        }
    }

    int missing = 0;
    for (int ci = 0; ci < codepoint_count; ci++) if (!covered[ci]) missing++;
    TraceLog(LOG_INFO, "MULTIFONT: \"%s\" — %d rasterized | %d missing | %d faces",
             primary_family, rasterized, missing, cache.count);

    uint8_t* rgba = malloc((size_t)(ATLAS_SIZE * ATLAS_SIZE * 4));
    for (int i = 0; i < ATLAS_SIZE * ATLAS_SIZE; i++) {
        rgba[i*4+0] = rgba[i*4+1] = rgba[i*4+2] = 255;
        rgba[i*4+3] = atlas.pixels[i];
    }

    Image img = { .data = rgba, .width = ATLAS_SIZE, .height = ATLAS_SIZE,
                  .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

    result.baseSize     = font_size;
    result.glyphCount   = codepoint_count;
    result.glyphPadding = GLYPH_PAD;
    result.texture      = LoadTextureFromImage(img);
    result.recs         = recs;
    result.glyphs       = glyphs;
    SetTextureFilter(result.texture, TEXTURE_FILTER_BILINEAR);

    free(fl); free(rgba); free(atlas.pixels); free(covered);
    face_cache_destroy(&cache);
    FT_Done_FreeType(ft);
    return result;
}

// ── CollectCodepoints (identique, pas de dépendance plateforme) ───────────────
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
        int i = 0, len = (int)strlen(s);
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
