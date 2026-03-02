/*
 * tr_core.h — Professional Unicode Text Renderer Core  v4
 *
 * Key improvements over v3:
 *  - Font itemization: each cluster is shaped with the face that has it
 *  - Combining/diacritic runs are shaped together with their base glyph
 *    using the SAME face → HarfBuzz gives x_advance=0 correctly
 *  - TR_AddFont() / TR_AddFontPriority() for explicit fallback chains
 *  - Unicode combining block detection for correct run splitting
 */

#ifndef TR_CORE_H
#define TR_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

/* Portable case-insensitive compare — works under -std=c99 */
static int tr__strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : (unsigned char)*a;
        int cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : (unsigned char)*b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

#include <ft2build.h>
#include FT_FREETYPE_H

#include "../../external/harfbuzz/src/hb.h"
#include "../../external/harfbuzz/src/hb-ft.h"


/* ── Platform ──────────────────────────────────────────────────────────── */

#ifdef _WIN32
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

/* ── Config ────────────────────────────────────────────────────────────── */

#ifndef TR_MAX_FONTS
  #define TR_MAX_FONTS   256
#endif
#ifndef TR_GLYPH_CACHE
  #define TR_GLYPH_CACHE 8192
#endif
#ifndef TR_CANVAS_PAD_TOP
  #define TR_CANVAS_PAD_TOP 3
#endif

/* ── Is codepoint a combining character? ───────────────────────────────── */
/*
 * Combining chars must NOT split runs — they must be shaped together with
 * their base glyph using the same face so HarfBuzz sets x_advance=0.
 */
static bool tr__is_combining(hb_codepoint_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F)   /* Combining Diacritical Marks       */
        || (cp >= 0x0483 && cp <= 0x0489)   /* Combining Cyrillic                */
        || (cp >= 0x0591 && cp <= 0x05C7)   /* Hebrew                            */
        || (cp >= 0x0610 && cp <= 0x061A)   /* Arabic                            */
        || (cp >= 0x064B && cp <= 0x065F)   /* Arabic vowels                     */
        || (cp >= 0x1AB0 && cp <= 0x1AFF)   /* Combining Diacritical Extended    */
        || (cp >= 0x1DC0 && cp <= 0x1DFF)   /* Combining Diacritical Supplement  */
        || (cp >= 0x20D0 && cp <= 0x20FF)   /* Combining for Symbols             */
        || (cp >= 0xFE20 && cp <= 0xFE2F)   /* Combining Half Marks              */
        /* Zalgo-specific blocks */
        || (cp >= 0x0338 && cp <= 0x0338)   /* Combining Long Solidus Overlay    */
        || (cp >= 0x20E3 && cp <= 0x20E3);  /* Combining Enclosing Keycap        */
}

/* ── Types ─────────────────────────────────────────────────────────────── */

typedef struct { float width; float height; } TR_Size;

typedef struct {
    unsigned char *bitmap;
    int            width, rows, pitch;
    int            draw_x, draw_y;  /* screen position relative to text origin */
    int            advance_x;
} TR_Glyph;

typedef struct {
    TR_Glyph *glyphs;
    int       count;
    int       total_advance;
    int       font_size;
} TR_ShapedText;

typedef struct {
    uint64_t       key;
    unsigned char *bitmap;
    int            width, rows, pitch;
    int            bitmap_left, bitmap_top;
    int            advance_x;
    bool           valid;
} TR__CachedGlyph;

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

/* ── Internal: directory scanning ──────────────────────────────────────── */

static bool tr__is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool tr__is_font_file(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    const char *e = name + n - 4;
    return tr__strcasecmp(e, ".ttf") == 0 ||
           tr__strcasecmp(e, ".otf") == 0 ||
           tr__strcasecmp(e, ".ttc") == 0;
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
        if (tr__is_dir(full))                    tr__scan_dir(tr, full);
        else if (tr__is_font_file(entry->d_name)) tr__load_font_file(tr, full);
    }
    closedir(dir);
}

/* ── Internal: glyph cache ─────────────────────────────────────────────── */

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

static hb_codepoint_t tr__utf8_decode(const unsigned char *s, int *bytes_out) {
    if ((*s & 0x80) == 0x00) { *bytes_out = 1; return *s; }
    if ((*s & 0xE0) == 0xC0) { *bytes_out = 2; return ((s[0]&0x1F)<<6)  | (s[1]&0x3F); }
    if ((*s & 0xF0) == 0xE0) { *bytes_out = 3; return ((s[0]&0x0F)<<12) | ((s[1]&0x3F)<<6)  | (s[2]&0x3F); }
    if ((*s & 0xF8) == 0xF0) { *bytes_out = 4; return ((s[0]&0x07)<<18) | ((s[1]&0x3F)<<12) | ((s[2]&0x3F)<<6) | (s[3]&0x3F); }
    *bytes_out = 1; return 0xFFFD;
}

/* ── Font itemization ───────────────────────────────────────────────────
 *
 * Walk the UTF-8 string and split it into runs.
 * Each run = a contiguous sequence of codepoints best covered by the
 * SAME face. Combining characters are ALWAYS merged into the run of the
 * preceding base character so HarfBuzz shapes them together.
 *
 * Returns: array of (start_byte, byte_length, face_index) triples.
 */

#define TR_MAX_RUNS 512

typedef struct {
    int start;      /* byte offset into utf8 string */
    int length;     /* byte length of this run      */
    int face_idx;   /* which face to use            */
} TR__Run;

static int tr__itemize(TR_Renderer *tr, const char *utf8,
                       TR__Run *runs, int max_runs) {
    int run_count   = 0;
    int i           = 0;
    int len         = (int)strlen(utf8);
    int cur_face    = -1;
    int run_start   = 0;

    while (i < len) {
        int bytes = 0;
        hb_codepoint_t cp = tr__utf8_decode((const unsigned char *)utf8 + i, &bytes);

        /* Combining chars join the previous run, never start a new one */
        bool is_combining = tr__is_combining(cp);

        int best_face;
        if (is_combining) {
            /* Stay with whatever face the previous run uses */
            best_face = (cur_face >= 0) ? cur_face : tr__find_face(tr, cp);
        } else {
            best_face = tr__find_face(tr, cp);
        }

        /* Start a new run if face changes (and this is not a combiner) */
        if (!is_combining && best_face != cur_face && cur_face >= 0) {
            if (run_count < max_runs) {
                runs[run_count].start    = run_start;
                runs[run_count].length   = i - run_start;
                runs[run_count].face_idx = cur_face;
                run_count++;
            }
            run_start = i;
        }

        cur_face = best_face;
        i += bytes;
    }

    /* Flush final run */
    if (i > run_start && run_count < max_runs) {
        runs[run_count].start    = run_start;
        runs[run_count].length   = i - run_start;
        runs[run_count].face_idx = cur_face >= 0 ? cur_face : 0;
        run_count++;
    }

    return run_count;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * TR_Create — scan system fonts, init renderer.
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
 * TR_AddFont — append a font to the END of the fallback chain.
 *   Use for supplemental/symbol fonts that fill gaps.
 */
static bool TR_AddFont(TR_Renderer *tr, const char *path) {
    int before = tr->face_count;
    tr__load_font_file(tr, path);
    return tr->face_count > before;
}

/*
 * TR_AddFontPriority — prepend a font to the FRONT of the fallback chain.
 *   Use for your primary/branded fonts that should take precedence.
 */
static bool TR_AddFontPriority(TR_Renderer *tr, const char *path) {
    int before = tr->face_count;
    tr__load_font_file(tr, path);
    int added = tr->face_count - before;
    if (added <= 0) return false;

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
 * TR_Shape — itemize by font, shape each run with HarfBuzz using the
 *   correct face, rasterize, return list of positioned TR_Glyph.
 *
 * This is the key fix for zalgo: each run containing a base + its
 * combining diacritics is shaped as a unit with a single face, so
 * HarfBuzz correctly assigns x_advance=0 to all combiners.
 */
static TR_ShapedText *TR_Shape(TR_Renderer *tr, const char *utf8) {
    if (!utf8 || !utf8[0]) {
        TR_ShapedText *st = (TR_ShapedText *)calloc(1, sizeof(TR_ShapedText));
        st->font_size = tr->font_size;
        return st;
    }

    TR__Run runs[TR_MAX_RUNS];
    int run_count = tr__itemize(tr, utf8, runs, TR_MAX_RUNS);

    /* First pass: count total glyphs across all runs */
    int total_glyphs = 0;
    for (int r = 0; r < run_count; r++) {
        hb_buffer_t *buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, utf8 + runs[r].start, runs[r].length, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        hb_shape(tr->hb_fonts[runs[r].face_idx], buf, NULL, 0);
        unsigned int n;
        hb_buffer_get_glyph_infos(buf, &n);
        total_glyphs += (int)n;
        hb_buffer_destroy(buf);
    }

    TR_ShapedText *st = (TR_ShapedText *)calloc(1, sizeof(TR_ShapedText));
    st->font_size = tr->font_size;
    st->glyphs    = (TR_Glyph *)calloc(total_glyphs + 1, sizeof(TR_Glyph));
    st->count     = 0;

    int baseline = tr->font_size * TR_CANVAS_PAD_TOP;
    int pen_x    = 0;

    /* Second pass: shape each run and fill glyphs */
    for (int r = 0; r < run_count; r++) {
        int face_idx = runs[r].face_idx;

        hb_buffer_t *buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, utf8 + runs[r].start, runs[r].length, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        hb_shape(tr->hb_fonts[face_idx], buf, NULL, 0);

        unsigned int n;
        hb_glyph_info_t     *info = hb_buffer_get_glyph_infos(buf, &n);
        hb_glyph_position_t *pos  = hb_buffer_get_glyph_positions(buf, &n);

        for (unsigned int i = 0; i < n; i++) {
            uint32_t glyph_id = info[i].codepoint;

            /* If HarfBuzz returned .notdef, try other faces */
            int actual_face = face_idx;
            if (glyph_id == 0) {
                /* Decode the original codepoint from cluster offset */
                const unsigned char *s = (const unsigned char *)utf8
                                         + runs[r].start + info[i].cluster;
                int bytes = 0;
                hb_codepoint_t cp = tr__utf8_decode(s, &bytes);
                actual_face = tr__find_face(tr, cp);
                glyph_id    = FT_Get_Char_Index(tr->faces[actual_face], cp);
            }

            TR__CachedGlyph *cg = tr__rasterize(tr, actual_face, glyph_id);
            TR_Glyph *g = &st->glyphs[st->count++];

            if (cg && cg->bitmap && cg->width > 0 && cg->rows > 0) {
                g->bitmap    = cg->bitmap;
                g->width     = cg->width;
                g->rows      = cg->rows;
                g->pitch     = cg->pitch;
                g->draw_x    = pen_x + (pos[i].x_offset >> 6) + cg->bitmap_left;
                g->draw_y    = baseline - (pos[i].y_offset >> 6) - cg->bitmap_top;
            } else {
                g->bitmap = NULL;
            }

            /* HarfBuzz advance is 0 for combining chars — correct by design */
            if (pos[i].x_advance != 0) {
                g->advance_x = pos[i].x_advance >> 6;
            } else if (glyph_id == 0 && cg) {
                g->advance_x = cg->advance_x; /* .notdef fallback */
            } else {
                g->advance_x = 0; /* combining: stay at same pen_x */
            }

            pen_x += g->advance_x;
        }

        hb_buffer_destroy(buf);
    }

    st->total_advance = pen_x;
    return st;
}

static void TR_FreeShapedText(TR_ShapedText *st) {
    if (!st) return;
    free(st->glyphs);
    free(st);
}

static TR_Size TR_Measure(TR_Renderer *tr, const char *utf8) {
    if (!utf8 || !utf8[0])
        return (TR_Size){ 0, (float)tr->font_size };

    TR__Run runs[TR_MAX_RUNS];
    int run_count = tr__itemize(tr, utf8, runs, TR_MAX_RUNS);

    float w = 0.0f;
    for (int r = 0; r < run_count; r++) {
        hb_buffer_t *buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, utf8 + runs[r].start, runs[r].length, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        hb_shape(tr->hb_fonts[runs[r].face_idx], buf, NULL, 0);

        unsigned int n;
        hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &n);
        for (unsigned int i = 0; i < n; i++)
            w += (float)(pos[i].x_advance >> 6);
        hb_buffer_destroy(buf);
    }

    return (TR_Size){ w, (float)tr->font_size };
}

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
