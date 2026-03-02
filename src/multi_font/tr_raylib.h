/*
 * tr_raylib.h — Raylib binding for tr_core.h
 *
 * Provides:
 *   TR_RL_RenderText()    — static text → Texture2D (one-shot, for labels/titles)
 *   TR_RL_DrawText()      — dynamic text, atlas-backed, call every frame
 *   TR_RL_MeasureText()   — Clay.h-compatible measurement function
 *   TR_RL_InitAtlas()     — must call once after InitWindow()
 *
 * Clay integration:
 *   TR_RL_Fonts fonts = { .renderer = tr, .font_size = 32 };
 *   Clay_SetMeasureTextFunction(TR_RL_MeasureText, &fonts);
 *
 * Include order:
 *   #include "tr_core.h"      <- always first
 *   #include "tr_raylib.h"
 */

#ifndef TR_RAYLIB_H
#define TR_RAYLIB_H

#include "tr_core.h"     /* must come first (defines NOGDI/NOUSER)   */
#include "raylib.h"
#include "rlgl.h"        /* rlUpdateTexture for partial atlas uploads */

/* ── Configuration ─────────────────────────────────────────────────────── */

#ifndef TR_RL_ATLAS_SIZE
  #define TR_RL_ATLAS_SIZE    2048
#endif
#ifndef TR_RL_ATLAS_PAD
  #define TR_RL_ATLAS_PAD     2
#endif
#ifndef TR_CANVAS_PAD_TOP
  #define TR_CANVAS_PAD_TOP   3
#endif
#ifndef TR_CANVAS_PAD_BOT
  #define TR_CANVAS_PAD_BOT   1
#endif

/* ── GPU glyph atlas ───────────────────────────────────────────────────── */

typedef struct {
    Texture2D tex;
    int       cursor_x;
    int       cursor_y;
    int       row_h;
    bool      ready;
} TR_RL_Atlas;

/* Per-glyph atlas location, stored parallel to TR_Renderer cache */
typedef struct {
    uint64_t key;
    int      atlas_x, atlas_y;
    int      width, rows;
    int      bitmap_left, bitmap_top;
    int      advance_x;
    bool     in_atlas;
} TR_RL_AtlasEntry;

#define TR_RL_ATLAS_ENTRIES TR_GLYPH_CACHE

/* Raylib-side state (kept separate from the core renderer)              */
typedef struct {
    TR_RL_Atlas      atlas;
    TR_RL_AtlasEntry entries[TR_RL_ATLAS_ENTRIES];
    int              entry_count;
} TR_RL_State;

/* Convenience bundle for Clay callback userData */
typedef struct {
    TR_Renderer *renderer;
    int          font_size;  /* used if you want dynamic sizing         */
} TR_RL_Fonts;

/* ════════════════════════════════════════════════════════════════════════
   Internal helpers
   ════════════════════════════════════════════════════════════════════════ */

static TR_RL_AtlasEntry *tr_rl__entry_get(TR_RL_State *s, uint64_t key) {
    for (int i = 0; i < s->entry_count; i++)
        if (s->entries[i].key == key) return &s->entries[i];
    return NULL;
}

/*
 * Upload one glyph bitmap into the GPU atlas as a partial texture update.
 * Returns the atlas entry (caller uses atlas_x/y for UV calculation).
 */
static TR_RL_AtlasEntry *tr_rl__ensure_in_atlas(TR_RL_State *s,
                                                  TR__CachedGlyph *cg,
                                                  int face_idx,
                                                  uint32_t glyph_idx) {
    uint64_t key = ((uint64_t)(uint32_t)face_idx << 32) | glyph_idx;
    TR_RL_AtlasEntry *e = tr_rl__entry_get(s, key);
    if (e) return e;
    if (s->entry_count >= TR_RL_ATLAS_ENTRIES) return NULL;

    e = &s->entries[s->entry_count++];
    e->key        = key;
    e->width      = cg->width;
    e->rows       = cg->rows;
    e->bitmap_left = cg->bitmap_left;
    e->bitmap_top  = cg->bitmap_top;
    e->advance_x   = cg->advance_x;
    e->in_atlas    = false;

    if (!cg->bitmap || cg->width == 0 || cg->rows == 0) {
        e->in_atlas = true; /* invisible glyph */
        return e;
    }

    TR_RL_Atlas *a = &s->atlas;

    /* Row overflow → start new row */
    if (a->cursor_x + cg->width + TR_RL_ATLAS_PAD > TR_RL_ATLAS_SIZE) {
        a->cursor_y += a->row_h + TR_RL_ATLAS_PAD;
        a->cursor_x  = TR_RL_ATLAS_PAD;
        a->row_h     = 0;
    }
    if (a->cursor_y + cg->rows + TR_RL_ATLAS_PAD > TR_RL_ATLAS_SIZE) {
        fprintf(stderr, "[TR_RL] GPU atlas full — increase TR_RL_ATLAS_SIZE\n");
        return e;
    }

    /* Grayscale → RGBA for atlas */
    unsigned char *rgba = (unsigned char *)malloc(cg->width * cg->rows * 4);
    if (!rgba) return e;
    for (int r = 0; r < cg->rows; r++) {
        for (int c = 0; c < cg->width; c++) {
            unsigned char v = cg->bitmap[r * abs(cg->pitch) + c];
            int idx = (r * cg->width + c) * 4;
            rgba[idx+0] = rgba[idx+1] = rgba[idx+2] = 255;
            rgba[idx+3] = v;
        }
    }

    /* Partial GPU upload — only touches the glyph rectangle            */
    rlUpdateTexture(a->tex.id,
                    a->cursor_x, a->cursor_y,
                    cg->width, cg->rows,
                    PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, rgba);
    free(rgba);

    e->atlas_x  = a->cursor_x;
    e->atlas_y  = a->cursor_y;
    e->in_atlas = true;

    a->cursor_x += cg->width + TR_RL_ATLAS_PAD;
    if (cg->rows > a->row_h) a->row_h = cg->rows;

    return e;
}

/* ════════════════════════════════════════════════════════════════════════
   Public API
   ════════════════════════════════════════════════════════════════════════ */

/*
 * TR_RL_InitAtlas — allocate the GPU texture atlas.
 *   Call ONCE after InitWindow(). Required for TR_RL_DrawText().
 *   Not needed for TR_RL_RenderText().
 */
static void TR_RL_InitAtlas(TR_RL_State *s) {
    if (s->atlas.ready) return;

    /* Start with a fully transparent atlas */
    unsigned char *blank = (unsigned char *)calloc(
        TR_RL_ATLAS_SIZE * TR_RL_ATLAS_SIZE * 4, 1);
    Image img = {
        .data    = blank,
        .width   = TR_RL_ATLAS_SIZE,
        .height  = TR_RL_ATLAS_SIZE,
        .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        .mipmaps = 1
    };
    s->atlas.tex      = LoadTextureFromImage(img);
    SetTextureFilter(s->atlas.tex, TEXTURE_FILTER_BILINEAR);
    free(blank);

    s->atlas.cursor_x = TR_RL_ATLAS_PAD;
    s->atlas.cursor_y = TR_RL_ATLAS_PAD;
    s->atlas.row_h    = 0;
    s->atlas.ready    = true;

    printf("[TR_RL] Atlas ready: %dx%d RGBA\n",
           TR_RL_ATLAS_SIZE, TR_RL_ATLAS_SIZE);
}

/*
 * TR_RL_UnloadAtlas — free GPU atlas texture.
 */
static void TR_RL_UnloadAtlas(TR_RL_State *s) {
    if (s->atlas.ready) {
        UnloadTexture(s->atlas.tex);
        s->atlas.ready = false;
    }
}

/* ── Mode A: Static (TR_RL_RenderText) ─────────────────────────────────
 *
 * Shapes, rasterizes, and uploads text to a brand-new Texture2D.
 * Use for text that changes rarely: titles, labels, pre-rendered UI.
 * Zero per-frame cost after creation.
 * Caller must UnloadTexture() when done.
 */
static Texture2D TR_RL_RenderText(TR_Renderer *tr,
                                   const char *utf8,
                                   Color tint) {
    /* Return 1×1 transparent texture for empty strings */
    if (!utf8 || !utf8[0]) {
        Image b = GenImageColor(1, 1, (Color){0,0,0,0});
        Texture2D t = LoadTextureFromImage(b);
        UnloadImage(b);
        return t;
    }

    TR_ShapedText *st = TR_Shape(tr, utf8);

    int canvas_w = st->total_advance + tr->font_size * 2;
    int canvas_h = tr->font_size * (TR_CANVAS_PAD_TOP + 1 + TR_CANVAS_PAD_BOT);
    if (canvas_w < 1) canvas_w = 1;
    if (canvas_h < 1) canvas_h = 1;

    unsigned char *canvas = (unsigned char *)calloc(canvas_w * canvas_h, 1);

    for (int i = 0; i < st->count; i++) {
        TR_Glyph *g = &st->glyphs[i];
        if (!g->bitmap || g->width == 0 || g->rows == 0) continue;

        for (int row = 0; row < g->rows; row++) {
            for (int col = 0; col < g->width; col++) {
                int cx = g->draw_x + col;
                int cy = g->draw_y + row;
                if (cx < 0 || cx >= canvas_w || cy < 0 || cy >= canvas_h) continue;
                unsigned char v = g->bitmap[row * abs(g->pitch) + col];
                /* MAX blend — diacritics never erase base glyphs        */
                if (v > canvas[cy * canvas_w + cx])
                    canvas[cy * canvas_w + cx] = v;
            }
        }
    }

    /* Grayscale → RGBA with tint */
    unsigned char *rgba = (unsigned char *)malloc(canvas_w * canvas_h * 4);
    for (int i = 0; i < canvas_w * canvas_h; i++) {
        unsigned char a = canvas[i];
        rgba[i*4+0] = (unsigned char)((int)tint.r * a / 255);
        rgba[i*4+1] = (unsigned char)((int)tint.g * a / 255);
        rgba[i*4+2] = (unsigned char)((int)tint.b * a / 255);
        rgba[i*4+3] = (unsigned char)((int)tint.a * a / 255);
    }

    Image img = {
        .data    = rgba,
        .width   = canvas_w,
        .height  = canvas_h,
        .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        .mipmaps = 1
    };
    Texture2D tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);

    free(rgba);
    free(canvas);
    TR_FreeShapedText(st);
    return tex;
}

/* ── Mode B: Dynamic (TR_RL_DrawText) ──────────────────────────────────
 *
 * Shapes text and draws it via the GPU atlas.
 * First call for any glyph: rasterize + partial GPU upload (~0.05ms).
 * Subsequent calls: pure quad draw, no CPU/GPU transfer (~0.001ms).
 * Use for scores, consoles, subtitles, any text that changes per frame.
 *
 * Requires TR_RL_InitAtlas() to have been called.
 */
static void TR_RL_DrawText(TR_Renderer *tr, TR_RL_State *s,
                            const char *utf8,
                            Vector2 pos, Color tint) {
    if (!utf8 || !utf8[0] || !s->atlas.ready) return;

    TR_ShapedText *st = TR_Shape(tr, utf8);

    /*
     * draw_y in TR_Glyph is relative to the static canvas top, where
     * baseline = font_size * TR_CANVAS_PAD_TOP.
     *
     * Clay passes pos.y as the TOP of the text box (same as DrawTextEx).
     * DrawTextEx places the first ascender at pos.y, which means the
     * baseline sits at pos.y + font_size (approx).
     *
     * To map canvas coordinates → screen coordinates:
     *   screen_y = pos.y + draw_y - font_size * (TR_CANVAS_PAD_TOP - 1)
     *
     * This shifts the canvas so its baseline aligns with pos.y + font_size,
     * matching DrawTextEx behaviour exactly.
     */
    float y_offset = -(float)(tr->font_size * (TR_CANVAS_PAD_TOP - 1));

    for (int i = 0; i < st->count; i++) {
        TR_Glyph *g = &st->glyphs[i];
        if (!g->bitmap || g->width == 0 || g->rows == 0) continue;

        TR__CachedGlyph *cg = NULL;
        uint64_t found_key  = 0;
        for (int ci = 0; ci < tr->cache_used; ci++) {
            if (tr->cache[ci].bitmap == g->bitmap) {
                cg        = &tr->cache[ci];
                found_key = cg->key;
                break;
            }
        }
        if (!cg) continue;

        int face_idx = (int)(found_key >> 32);
        uint32_t gi  = (uint32_t)(found_key & 0xFFFFFFFF);

        TR_RL_AtlasEntry *e = tr_rl__ensure_in_atlas(s, cg, face_idx, gi);
        if (!e || !e->in_atlas) continue;

        Rectangle src = {
            (float)e->atlas_x, (float)e->atlas_y,
            (float)e->width,   (float)e->rows
        };
        Rectangle dst = {
            pos.x + (float)g->draw_x,
            pos.y + (float)g->draw_y + y_offset,
            (float)g->width, (float)g->rows
        };
        DrawTexturePro(s->atlas.tex, src, dst, (Vector2){0,0}, 0.0f, tint);
    }

    TR_FreeShapedText(st);
}

/* ── Clay.h MeasureText binding ─────────────────────────────────────────
 *
 * Drop-in for Clay_SetMeasureTextFunction:
 *
 *   TR_RL_Fonts fonts = { .renderer = tr };
 *   Clay_SetMeasureTextFunction(TR_RL_MeasureText, &fonts);
 *
 * Clay passes a Clay_StringSlice (not null-terminated!) so we copy it.
 * Clay_TextElementConfig carries fontSize, letterSpacing, lineHeight.
 *
 * If you use a fixed font size (most common), just ignore config->fontSize.
 * If you support multiple sizes, create one TR_Renderer per size.
 */

/* clay.h must be included before tr_raylib.h so its types are available. */
#ifndef CLAY_HEADER
  #error "Include clay.h before tr_raylib.h"
#endif

static Clay_Dimensions TR_RL_MeasureText(Clay_StringSlice    text,
                                          Clay_TextElementConfig *config,
                                          void               *userData) {
    TR_RL_Fonts *fonts = (TR_RL_Fonts *)userData;
    if (!fonts || !fonts->renderer || text.length <= 0)
        return (Clay_Dimensions){0, 0};

    /* Clay_StringSlice is NOT null-terminated — copy to temp buffer   */
    char tmp[4096];
    int  len = text.length < (int)sizeof(tmp)-1 ? text.length : (int)sizeof(tmp)-1;
    memcpy(tmp, text.chars, len);
    tmp[len] = '\0';

    TR_Size sz = TR_Measure(fonts->renderer, tmp);

    /* If Clay provides a fontSize override, scale proportionally      */
    if (config && config->fontSize > 0 && config->fontSize != fonts->renderer->font_size) {
        float scale = (float)config->fontSize / (float)fonts->renderer->font_size;
        sz.width  *= scale;
        sz.height *= scale;
    }

    return (Clay_Dimensions){ sz.width, (float)fonts->renderer->font_size };
}

#endif /* TR_RAYLIB_H */
