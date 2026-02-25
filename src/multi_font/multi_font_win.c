#include "../../freetype_build/include/ft2build.h"
#include "../../freetype/include/freetype/freetype.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../raylib/include/raylib.h"

#include <ctype.h>

// Replace the helper in registry_find_family:
static void str_to_lower(char* s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

// ──────────────────────────────────────────────────────────────────────────────
// Config
// ──────────────────────────────────────────────────────────────────────────────
#define ATLAS_SIZE    4096
#define GLYPH_PAD     2
#define FACE_CACHE_MAX 64
#define MAX_FONT_FILES 2048
#define MAX_PATH_LEN   2048

// ──────────────────────────────────────────────────────────────────────────────
// Atlas
// ──────────────────────────────────────────────────────────────────────────────
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

// ──────────────────────────────────────────────────────────────────────────────
// Face cache  (identical to original)
// ──────────────────────────────────────────────────────────────────────────────
typedef struct { char path[MAX_PATH_LEN]; FT_Face face; } FaceEntry;
typedef struct {
    FaceEntry  entries[FACE_CACHE_MAX];
    int        count;
    FT_Library ft;
    int        pixel_size;
} FaceCache;

static FT_Face face_cache_get_or_load(FaceCache* c, const char* path) {
    for (int i = 0; i < c->count; i++)
        if (_stricmp(c->entries[i].path, path) == 0)   // case-insensitive on Win
            return c->entries[i].face;

    if (c->count >= FACE_CACHE_MAX) {
        TraceLog(LOG_WARNING, "MULTIFONT: face cache full, skipping %s", path);
        return NULL;
    }
    FT_Face face;
    if (FT_New_Face(c->ft, path, 0, &face) != 0) return NULL;
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)c->pixel_size);

    strncpy(c->entries[c->count].path, path, MAX_PATH_LEN - 1);
    c->entries[c->count].path[MAX_PATH_LEN - 1] = '\0';
    c->entries[c->count].face = face;
    c->count++;
    TraceLog(LOG_INFO, "MULTIFONT: loaded face [%d] %s", c->count - 1, path);
    return face;
}

static void face_cache_destroy(FaceCache* c) {
    for (int i = 0; i < c->count; i++) FT_Done_Face(c->entries[i].face);
    c->count = 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Win32 font discovery  (replaces fontconfig)
// ──────────────────────────────────────────────────────────────────────────────

// Resolved list of absolute font file paths, priority-ordered:
//   [0..named_count)  = explicitly named families  (registry lookup)
//   [named_count..)   = everything else in the Fonts dir (fallback sweep)
typedef struct {
    char   paths[MAX_FONT_FILES][MAX_PATH_LEN];
    int    count;
    int    named_count;   // how many came from explicit name lookups
} FontList;

// Returns the Windows Fonts directory, e.g. "C:\Windows\Fonts"
static void get_fonts_dir(char* out, int out_len) {
    // SHGetFolderPath would be nicer but this avoids Shell32 linkage
    char windir[MAX_PATH_LEN];
    UINT n = GetWindowsDirectoryA(windir, sizeof(windir));
    if (n == 0) strncpy(windir, "C:\\Windows", sizeof(windir));
    snprintf(out, out_len, "%s\\Fonts", windir);
}

// Push a path into FontList, deduplicating (case-insensitive).
static void fontlist_push(FontList* fl, const char* path) {
    if (fl->count >= MAX_FONT_FILES) return;
    for (int i = 0; i < fl->count; i++)
        if (_stricmp(fl->paths[i], path) == 0) return;
    strncpy(fl->paths[fl->count], path, MAX_PATH_LEN - 1);
    fl->paths[fl->count][MAX_PATH_LEN - 1] = '\0';
    fl->count++;
}

// Registry key that maps "Font Name (TrueType)" → "filename.ttf"
#define FONTS_REG_KEY \
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"

// Look up every registry value whose name contains `family` (case-insensitive).
// Pushes full paths into fl.
static void registry_find_family(FontList* fl,
                                  const char* family,
                                  const char* fonts_dir) {
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, FONTS_REG_KEY,
                      0, KEY_READ, &hkey) != ERROR_SUCCESS) return;

    char   val_name[256];
    char   val_data[MAX_PATH_LEN];
    DWORD  idx = 0;

    while (true) {
        DWORD name_len = sizeof(val_name);
        DWORD data_len = sizeof(val_data);
        DWORD type;

        LONG rc = RegEnumValueA(hkey, idx++,
                                val_name, &name_len,
                                NULL, &type,
                                (BYTE*)val_data, &data_len);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS)       continue;
        if (type != REG_SZ)            continue;

        // val_name is like "Segoe UI (TrueType)" — check if family is in it
        // Simple case-insensitive substring match
        char lower_name[256], lower_family[256];
        strncpy(lower_name,   val_name, 255);
        strncpy(lower_family, family,   255);
        str_to_lower(lower_name);
        str_to_lower(lower_family);
        if (strstr(lower_name, lower_family) == NULL) continue;

        // val_data may be a bare filename or a full path
        char full[MAX_PATH_LEN];
        if (strchr(val_data, '\\') || strchr(val_data, '/')) {
            strncpy(full, val_data, MAX_PATH_LEN - 1);
        } else {
            snprintf(full, MAX_PATH_LEN, "%s\\%s", fonts_dir, val_data);
        }
        fontlist_push(fl, full);
    }
    RegCloseKey(hkey);
}

// Scan the Fonts directory and append every .ttf / .otf / .ttc file.
static void scan_fonts_dir(FontList* fl, const char* fonts_dir) {
    char pattern[MAX_PATH_LEN];
    snprintf(pattern, MAX_PATH_LEN, "%s\\*.*", fonts_dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        const char* dot = strrchr(fd.cFileName, '.');
        if (!dot) continue;
        char ext[8];
        strncpy(ext, dot + 1, 7);
        str_to_lower(ext);
        if (strcmp(ext, "ttf") != 0 &&
            strcmp(ext, "otf") != 0 &&
            strcmp(ext, "ttc") != 0) continue;

        char full[MAX_PATH_LEN];
        snprintf(full, MAX_PATH_LEN, "%s\\%s", fonts_dir, fd.cFileName);
        fontlist_push(fl, full);
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

// Build the priority-sorted FontList replacing FcFontSort.
//   primary_family      → highest priority
//   preferred_families  → next in order
//   everything else     → fallback
static FontList* build_font_list(const char*  primary_family,
                                  const char** preferred_families,
                                  int          preferred_count) {
    FontList* fl = calloc(1, sizeof(FontList));   // heap, not stack
    char fonts_dir[MAX_PATH_LEN];
    get_fonts_dir(fonts_dir, sizeof(fonts_dir));

    registry_find_family(fl, primary_family, fonts_dir);
    for (int i = 0; i < preferred_count; i++)
        registry_find_family(fl, preferred_families[i], fonts_dir);

    fl->named_count = fl->count;
    scan_fonts_dir(fl, fonts_dir);

    TraceLog(LOG_INFO, "MULTIFONT: font list built — %d named, %d total",
             fl->named_count, fl->count);
    return fl;
}

// ──────────────────────────────────────────────────────────────────────────────
// BuildMultiFontAtlas  (public API — identical signature to the original)
// ──────────────────────────────────────────────────────────────────────────────
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

    FontList* fl = build_font_list(primary_family, preferred_families, preferred_count);
    if (fl->count == 0) {
        TraceLog(LOG_ERROR, "MULTIFONT: no fonts found");
        free(fl);
        FT_Done_FreeType(ft);
        return result;
    }

    // Glyph arrays
    GlyphInfo* glyphs  = calloc(codepoint_count, sizeof(GlyphInfo));
    Rectangle* recs    = calloc(codepoint_count, sizeof(Rectangle));
    bool*      covered = calloc(codepoint_count, sizeof(bool));

    Atlas atlas = {
        .pixels   = calloc(ATLAS_SIZE * ATLAS_SIZE, 1),
        .cursor_x = GLYPH_PAD,
        .cursor_y = GLYPH_PAD,
        .row_h    = 0,
    };

    int fallback_advance = font_size / 2;
    for (int ci = 0; ci < codepoint_count; ci++) {
        glyphs[ci].value    = codepoints[ci];
        glyphs[ci].advanceX = fallback_advance;
        recs[ci]            = (Rectangle){ 0, 0, 0, 0 };
    }

    int  rasterized = 0;
    int  remaining  = codepoint_count;
    bool atlas_full = false;

    // Walk fonts in priority order (named first, then directory fallbacks)
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

            int w = (int)bm->width;
            int h = (int)bm->rows;

            if (w == 0 || h == 0) continue;
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

    // Grayscale → RGBA
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

    free(fl);
    free(rgba);
    free(atlas.pixels);
    free(covered);
    face_cache_destroy(&cache);
    FT_Done_FreeType(ft);
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// CollectCodepoints  (unchanged — no platform dependency)
// ──────────────────────────────────────────────────────────────────────────────
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
