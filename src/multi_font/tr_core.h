/*
 * tr_core.h — Renderer-agnostic Unicode text shaping core
 * Depends only on FreeType + HarfBuzz. Zero Raylib, Zero OpenGL.
 *
 * Responsibilities:
 *   - System font discovery + TTC support
 *   - Per-codepoint fallback chain
 *   - HarfBuzz shaping (combining chars, RTL, CJK, emoji, zalgo…)
 *   - CPU-side glyph rasterization + bitmap cache
 *   - MeasureText
 *   - TR_Shape() → TR_ShapedText: list of positioned glyph bitmaps
 *     that any renderer can consume.
 *
 * A renderer binding (e.g. tr_raylib.h) takes TR_ShapedText and
 * produces whatever GPU resource it needs.
 */

#ifndef TR_CORE_H
#define TR_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>

/* ── Platform: directory scanning ──────────────────────────────────────── */

#ifdef _WIN32
  /* Strip GDI/USER symbols before windows.h to avoid collisions with
     any downstream renderer header (Raylib, SDL, etc.)               */
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOGDI
    #define NOGDI
  #endif
  #ifndef NOUSER
    #define NOUSER
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <sys/stat.h>
  #include <dirent.h>
  static const char *TR__FONT_DIRS[] = { "C:/Windows/Fonts", NULL };
#elif defined(__APPLE__)
  #include <dirent.h>
  #include <sys/stat.h>
  static const char *TR__FONT_DIRS[] = {
    "/System/Library/Fonts", "/Library/Fonts",
    "/System/Library/Fonts/Supplemental", NULL
  };
#else
  #include <dirent.h>
  #include <sys/stat.h>
  static const char *TR__FONT_DIRS[] = {
    "/usr/share/fonts", "/usr/local/share/fonts", NULL
  };
#endif

/* ── Configuration ─────────────────────────────────────────────────────── */

#ifndef TR_MAX_FONTS
  #define TR_MAX_FONTS   256   /* handles many TTC faces               */
#endif
#ifndef TR_GLYPH_CACHE
  #define TR_GLYPH_CACHE 8192  /* CPU bitmap cache entries             */
#endif

/* ── Types ─────────────────────────────────────────────────────────────── */

/* Axis-aligned size in pixels */
typedef struct { float width; float height; } TR_Size;

/* A single rasterized glyph, ready for a renderer to consume */
typedef struct {
    /* Bitmap (grayscale, 1 byte per pixel) */
    unsigned char *bitmap;   /* NULL for invisible glyphs (combiners) */
    int            width;
    int            rows;
    int            pitch;    /* bytes per row (may differ from width)  */

    /* Placement relative to the shaped-text origin */
    int draw_x;     /* pen_x + x_offset + bitmap_left                 */
    int draw_y;     /* baseline + y_offset - bitmap_top                */

    /* Advance (0 for combining/diacritic glyphs)                      */
    int advance_x;
} TR_Glyph;

/* Output of TR_Shape() — caller must TR_FreeShapedText() it            */
typedef struct {
    TR_Glyph *glyphs;
    int       count;
    int       total_advance; /* sum of all advance_x values            */
    int       font_size;     /* inherited from renderer                */
} TR_ShapedText;

/* CPU-side glyph bitmap cache entry                                    */
typedef struct {
    uint64_t       key;        /* (face_idx << 32) | glyph_index       */
    unsigned char *bitmap;
    int            width, rows, pitch;
    int            bitmap_left, bitmap_top;
    int            advance_x;
    bool           valid;
} TR__CachedGlyph;

/* Main renderer state (opaque to bindings; accessed via TR_* functions) */
typedef struct TR_Renderer TR_Renderer;
struct TR_Renderer {
    FT_Library  ft;
    FT_Face     faces[TR_MAX_FONTS];
    hb_font_t  *hb_fonts[TR_MAX_FONTS];
    int         face_count;
    int         font_size;

    TR__CachedGlyph cache[TR_GLYPH_CACHE];
    int             cache_used;
};

/* ════════════════════════════════════════════════════════════════════════
   Internal helpers (tr__ prefix)
   ════════════════════════════════════════════════════════════════════════ */

static bool tr__is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool tr__is_font_file(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    const char *e = name + n - 4;
    return strcasecmp(e, ".ttf") == 0 ||
           strcasecmp(e, ".otf") == 0 ||
           strcasecmp(e, ".ttc") == 0;
}

static void tr__load_font_file(TR_Renderer *tr, const char *path) {
    FT_Long num_faces = 1;
    { FT_Face p; if (FT_New_Face(tr->ft, path, 0, &p)) return;
      num_faces = p->num_faces; FT_Done_Face(p); }

    for (FT_Long fi = 0; fi < num_faces && tr->face_count < TR_MAX_FONTS; fi++) {
        FT_Face face;
        if (FT_New_Face(tr->ft, path, fi, &face)) continue;
        if (!FT_IS_SCALABLE(face)) { FT_Done_Face(face); continue; }
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)tr->font_size);
        tr->faces[tr->face_count]    = face;
        tr->hb_fonts[tr->face_count] = hb_ft_font_create(face, NULL);
        tr->face_count++;
    }
}

static void tr__scan_dir(TR_Renderer *tr, const char *dir_path) {
    if (tr->face_count >= TR_MAX_FONTS) return;
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) && tr->face_count < TR_MAX_FONTS) {
        if (entry->d_name[0] == '.') continue;
        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);
        if (tr__is_dir(full))              tr__scan_dir(tr, full);
        else if (tr__is_font_file(entry->d_name)) tr__load_font_file(tr, full);
    }
    closedir(dir);
}

static int tr__find_face(TR_Renderer *tr, hb_codepoint_t cp) {
    for (int i = 0; i < tr->face_count; i++)
        if (FT_Get_Char_Index(tr->faces[i], cp)) return i;
    return 0;
}

static TR__CachedGlyph *tr__cache_get(TR_Renderer *tr, int fi, uint32_t gi) {
    uint64_t key = ((uint64_t)(uint32_t)fi << 32) | gi;
    for (int i = 0; i < tr->cache_used; i++)
        if (tr->cache[i].key == key) return &tr->cache[i];
    return NULL;
}

static TR__CachedGlyph *tr__rasterize(TR_Renderer *tr, int face_idx, uint32_t glyph_idx) {
    TR__CachedGlyph *c = tr__cache_get(tr, face_idx, glyph_idx);
    if (c) return c;
    if (tr->cache_used >= TR_GLYPH_CACHE) return NULL;

    FT_Face face = tr->faces[face_idx];
    if (FT_Load_Glyph(face, glyph_idx, FT_LOAD_RENDER)) return NULL;

    FT_Bitmap *bmp = &face->glyph->bitmap;
    c = &tr->cache[tr->cache_used++];
    c->key         = ((uint64_t)(uint32_t)face_idx << 32) | glyph_idx;
    c->width       = (int)bmp->width;
    c->rows        = (int)bmp->rows;
    c->pitch       = bmp->pitch;
    c->bitmap_left = face->glyph->bitmap_left;
    c->bitmap_top  = face->glyph->bitmap_top;
    c->advance_x   = (int)(face->glyph->advance.x >> 6);
    c->valid       = true;
    c->bitmap      = NULL;

    if (c->width > 0 && c->rows > 0) {
        int sz = c->rows * abs(c->pitch);
        c->bitmap = (unsigned char *)malloc(sz);
        if (c->bitmap) memcpy(c->bitmap, bmp->buffer, sz);
    }
    return c;
}

static hb_codepoint_t tr__utf8_decode(const unsigned char *s) {
    if ((*s & 0x80) == 0x00) return *s;
    if ((*s & 0xE0) == 0xC0) return ((s[0]&0x1F)<<6)  | (s[1]&0x3F);
    if ((*s & 0xF0) == 0xE0) return ((s[0]&0x0F)<<12) | ((s[1]&0x3F)<<6)  | (s[2]&0x3F);
    if ((*s & 0xF8) == 0xF0) return ((s[0]&0x07)<<18) | ((s[1]&0x3F)<<12) | ((s[2]&0x3F)<<6) | (s[3]&0x3F);
    return 0xFFFD;
}

/* ════════════════════════════════════════════════════════════════════════
   Public API
   ════════════════════════════════════════════════════════════════════════ */

/*
 * TR_Create — discover system fonts, init FreeType + HarfBuzz.
 *   fontSize: target render height in pixels.
 *   Returns NULL on failure.
 */
static TR_Renderer *TR_Create(int fontSize) {
    TR_Renderer *tr = (TR_Renderer *)calloc(1, sizeof(TR_Renderer));
    if (!tr) return NULL;
    tr->font_size = fontSize;

    if (FT_Init_FreeType(&tr->ft)) {
        fprintf(stderr, "[TR] FreeType init failed\n");
        free(tr); return NULL;
    }
    for (int d = 0; TR__FONT_DIRS[d]; d++)
        tr__scan_dir(tr, TR__FONT_DIRS[d]);

    if (!tr->face_count) {
        fprintf(stderr, "[TR] No system fonts found\n");
        free(tr); return NULL;
    }
    printf("[TR] %d font faces loaded\n", tr->face_count);
    return tr;
}

/*
 * TR_AddFont — prepend a font file to the fallback chain (highest priority).
 *   Call after TR_Create, before any rendering.
 *   Supports TTC: every face inside the file is prepended.
 */
static bool TR_AddFont(TR_Renderer *tr, const char *path) {
    int before = tr->face_count;
    tr__load_font_file(tr, path);
    int added = tr->face_count - before;
    if (added <= 0) return false;

    /* Rotate the newly appended faces to index 0 (highest priority) */
    for (int i = 0; i < added; i++) {
        FT_Face    fF = tr->faces[tr->face_count - 1];
        hb_font_t *fH = tr->hb_fonts[tr->face_count - 1];
        memmove(&tr->faces[1],    &tr->faces[0],    (tr->face_count-1)*sizeof(FT_Face));
        memmove(&tr->hb_fonts[1],&tr->hb_fonts[0],(tr->face_count-1)*sizeof(hb_font_t*));
        tr->faces[0] = fF; tr->hb_fonts[0] = fH;
    }
    return true;
}

/*
 * TR_Shape — shape UTF-8 text and rasterize all glyphs.
 *   Returns a TR_ShapedText with one TR_Glyph per shaped unit,
 *   including invisible combiners (bitmap == NULL, advance_x == 0).
 *   Caller must call TR_FreeShapedText() when done.
 */
static TR_ShapedText *TR_Shape(TR_Renderer *tr, const char *utf8) {
    if (!utf8 || !utf8[0]) {
        TR_ShapedText *st = (TR_ShapedText *)calloc(1, sizeof(TR_ShapedText));
        st->font_size = tr->font_size;
        return st;
    }

    /* ── HarfBuzz shape ─────────────────────────────────────────────── */
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(tr->hb_fonts[0], buf, NULL, 0);

    unsigned int n;
    hb_glyph_info_t     *hb_info = hb_buffer_get_glyph_infos(buf, &n);
    hb_glyph_position_t *hb_pos  = hb_buffer_get_glyph_positions(buf, &n);

    TR_ShapedText *st = (TR_ShapedText *)calloc(1, sizeof(TR_ShapedText));
    st->font_size = tr->font_size;
    st->glyphs    = (TR_Glyph *)calloc(n, sizeof(TR_Glyph));
    st->count     = (int)n;

    /* baseline for draw_y calculation                                  */
    int baseline = tr->font_size * 3; /* matches CANVAS_PAD_TOP = 3    */
    int pen_x    = 0;

    for (unsigned int i = 0; i < n; i++) {
        hb_codepoint_t cp = tr__utf8_decode(
            (const unsigned char *)utf8 + hb_info[i].cluster);

        int      face_idx    = 0;
        uint32_t actual_glyph = hb_info[i].codepoint;

        if (actual_glyph == 0) {
            /* .notdef from primary — look up fallback face */
            face_idx     = tr__find_face(tr, cp);
            actual_glyph = FT_Get_Char_Index(tr->faces[face_idx], cp);
        }

        TR__CachedGlyph *cg = tr__rasterize(tr, face_idx, actual_glyph);
        TR_Glyph *g = &st->glyphs[i];

        if (cg && cg->bitmap && cg->width > 0 && cg->rows > 0) {
            /* Copy bitmap reference — ShapedText shares the cache data  */
            g->bitmap    = cg->bitmap;
            g->width     = cg->width;
            g->rows      = cg->rows;
            g->pitch     = cg->pitch;
            g->draw_x    = pen_x + (hb_pos[i].x_offset >> 6) + cg->bitmap_left;
            g->draw_y    = baseline - (hb_pos[i].y_offset >> 6) - cg->bitmap_top;
        } else {
            g->bitmap = NULL;
        }

        /* Advance */
        if (hb_pos[i].x_advance) {
            g->advance_x = hb_pos[i].x_advance >> 6;
        } else if (hb_info[i].codepoint == 0 && cg) {
            g->advance_x = cg->advance_x;  /* fallback face advance */
        } else {
            g->advance_x = 0;              /* combining char        */
        }

        pen_x           += g->advance_x;
        st->total_advance = pen_x;
    }

    hb_buffer_destroy(buf);
    return st;
}

/*
 * TR_FreeShapedText — release a TR_ShapedText.
 *   Does NOT free the bitmaps (those belong to the glyph cache).
 */
static void TR_FreeShapedText(TR_ShapedText *st) {
    if (!st) return;
    free(st->glyphs);
    free(st);
}

/*
 * TR_Measure — measure a UTF-8 string without rasterizing.
 *   Fast path: just sum HarfBuzz advances, no bitmap work.
 */
static TR_Size TR_Measure(TR_Renderer *tr, const char *utf8) {
    if (!utf8 || !utf8[0])
        return (TR_Size){ 0, (float)(tr->font_size * 5) };

    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(tr->hb_fonts[0], buf, NULL, 0);

    unsigned int n;
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &n);

    float w = (float)(tr->font_size * 2); /* padding */
    for (unsigned int i = 0; i < n; i++)
        w += (float)(pos[i].x_advance >> 6);

    hb_buffer_destroy(buf);
    return (TR_Size){
        w,
        (float)(tr->font_size * 5) /* PAD_TOP(3) + 1 + PAD_BOT(1) */
    };
}

/*
 * TR_Destroy — free all resources.
 */
static void TR_Destroy(TR_Renderer *tr) {
    if (!tr) return;
    for (int i = 0; i < tr->cache_used; i++) free(tr->cache[i].bitmap);
    for (int i = 0; i < tr->face_count; i++) {
        hb_font_destroy(tr->hb_fonts[i]);
        FT_Done_Face(tr->faces[i]);
    }
    FT_Done_FreeType(tr->ft);
    free(tr);
}

#endif /* TR_CORE_H */
