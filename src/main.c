#include <math.h>
#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define CLAY_IMPLEMENTATION
#include "../external/clay.h"
#include "../external/raylib_renderer.c"
#define RAYLIB_VECTOR2_TO_CLAY_VECTOR2(vector) (Clay_Vector2) { .x = vector.x, .y = vector.y }

#define VECTOR_IMPLEMENTATION
#include "vector.h"
#define ARENA_IMPLEMENTATION
#include "arena.h"

#include "metadata.c"

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600

#define COLOR_ORANGE (Clay_Color) {225, 138, 50, 255}
#define COLOR_BLUE (Clay_Color) {111, 173, 162, 255}

#define COLOR_BACKGROUND (Clay_Color) {20, 20, 20, 255}
#define COLOR_BACKGROUND_LIGHT (Clay_Color) {40, 40, 40, 255}

#define DOUBLE_CLICK_DELAY 0.25 // secondes

typedef Vector Playlist;

typedef enum {
    PLAYING,
    PAUSED,
    STOPPED,
} PlayerState;

typedef struct {
    char* path;
    char* title;
    char* artists;
    char* album;
    float_t length; // second
    ImageBuffer* img;
} Song;

typedef struct {
    Playlist* songs;
    int32_t current_index_selected;
    int32_t current_index_playing;
} Queue;

typedef struct {
    Queue* queue;
    PlayerState state;
} Context;

typedef struct {
    bool active;        // currently dragging
    float value;        // 0..1 normalized
} SliderState;

typedef struct {
    char* buf;
    Clay_String clay;
} UiTimeString;


typedef struct {
    Clay_Vector2 clickOrigin;
    Clay_Vector2 positionOrigin;
    bool mouseDown;
} ScrollbarData;

void selectIndex(Queue* queue, int index);
void playIndex(Queue* queue, int index);
Song* createSong(mem_arena* arena, char* filepath);
bool addSongToQueue(Queue* queue, Song* song, int index);
bool removeSongFromQueue(Queue* queue, int index);
Texture2D texture_from_image_buffer(ImageBuffer* img);

// CLAY RENDER
void HandleClayErrors(Clay_ErrorData errorData);
bool reinitializeClay = false;
bool debugEnabled = false;
ScrollbarData scrollbarData = {0};
static SliderState music_slider = {0};

// Player
Context ctx = {0};
Music music = {0};
Vector covers_textures = {0};
mem_arena* song_arena = {0};
mem_arena* ui_arena;


void seconds_to_hms(double seconds, int *h, int *m, int *s)
{
    int total = (int)round(seconds);  // ou floor() selon ton besoin

    *h = total / 3600;
    total %= 3600;

    *m = total / 60;
    *s = total % 60;
}

UiTimeString MakeTimeStringFromFloat(mem_arena* arena, float seconds)
{
    int h, m, s;
    seconds_to_hms(seconds, &h, &m, &s);

    char* buf = arena_push(arena, 16, false);

    snprintf(buf, 16, "%02d:%02d:%02d", h, m, s);

    return (UiTimeString){
        .buf = buf,
        .clay = {
            .chars = buf,
            .length = 8,
            .isStaticallyAllocated = false
        }
    };
}


void HandleSongInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    int index = (int)(intptr_t)userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME || Clay_PointerOver(elementId)) {
        selectIndex(ctx.queue, index);
    }
}

void RenderSong(Song song, int index) {
    CLAY_AUTO_ID({ .layout = { .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(80)}, .childGap = 8 }, .backgroundColor =  (index == ctx.queue->current_index_selected) ? COLOR_BLUE : (index == ctx.queue->current_index_playing) ? COLOR_ORANGE : (Clay_Hovered() ? COLOR_BACKGROUND : COLOR_BACKGROUND_LIGHT)}) {
        Clay_Color color = (index == ctx.queue->current_index_selected) ? COLOR_BLUE : (index == ctx.queue->current_index_playing) ? COLOR_ORANGE : (Clay_Hovered() ? COLOR_BACKGROUND : COLOR_BACKGROUND_LIGHT);
        Clay_OnHover(HandleSongInteraction, (void*)(intptr_t)index);
        CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)} }, .image = { .imageData = vectorGet(&covers_textures, index)}, .aspectRatio = { 1 }}) {}
        CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .padding = {8, 8, 8, 8}, .childGap = 16 }, .backgroundColor = color}) {
            Clay_String string_title = { .chars = song.title, .length = strlen(song.title), .isStaticallyAllocated = false };
            CLAY_TEXT(string_title, CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));
            Clay_String string_artists = { .chars = song.artists, .length = strlen(song.artists), .isStaticallyAllocated = false };
            CLAY_TEXT(string_artists, CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));
        }

        Clay_String string_album = { .chars = song.album, .length = strlen(song.album), .isStaticallyAllocated = false };
        CLAY_TEXT(string_album, CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));

        UiTimeString time = MakeTimeStringFromFloat(ui_arena, song.length);
        CLAY_TEXT(time.clay , CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));
    }
}
void HandleSliderInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;
    (void)userData;
    if (!IsMusicValid(music))
        return;

    float length = GetMusicTimeLength(music);
    if (length <= 0.0f)
        return;

    Clay_ElementId sliderId = Clay_GetElementId(CLAY_STRING("SLIDER"));
    Clay_ElementData data = Clay_GetElementData(sliderId);

    if (!data.found)
        return;

    Clay_BoundingBox bb = data.boundingBox;
    if (bb.width <= 0.0f || bb.height <= 0.0f)
        return;

    Clay_Vector2 m = pointerInfo.position;

    bool mouseOver =
        m.x >= bb.x && m.x <= bb.x + bb.width &&
        m.y >= bb.y && m.y <= bb.y + bb.height;

    /* ---------- start drag ---------- */
    if (pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME && mouseOver) {
        music_slider.active = true;
    }

    /* ---------- dragging ---------- */
    if (music_slider.active &&
        pointerInfo.state == CLAY_POINTER_DATA_PRESSED) {

        float localX = m.x - bb.x;
        music_slider.value = localX / bb.width;

        if (music_slider.value < 0.f) music_slider.value = 0.f;
        if (music_slider.value > 1.f) music_slider.value = 1.f;
    }

    /* ---------- release ---------- */
    if (music_slider.active &&
        pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME) {

        SeekMusicStream(music, music_slider.value * length);
        music_slider.active = false;
    }

    /* ---------- sync from playback ---------- */
    if (!music_slider.active) {
        music_slider.value = GetMusicTimePlayed(music) / length;
        if (music_slider.value < 0.f) music_slider.value = 0.f;
        if (music_slider.value > 1.f) music_slider.value = 1.f;
    }
}

int main(void)
{
    // Clay initialisation
    uint64_t totalMemorySize = Clay_MinMemorySize();
    Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));
    Clay_Initialize(clayMemory, (Clay_Dimensions) { (float)DEFAULT_WIDTH, (float)DEFAULT_HEIGHT }, (Clay_ErrorHandler) { HandleClayErrors, 0 });

    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "Music Player");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetWindowState(FLAG_MSAA_4X_HINT);
    InitAudioDevice();
    SetTargetFPS(60);

    Font fonts[1];

    fonts[0] = LoadFontEx("../assets/fonts/Poppins/Poppins-Regular.ttf", 24, NULL, 0);
   	SetTextureFilter(fonts[0].texture, TEXTURE_FILTER_BILINEAR);
    Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);


    Playlist songs = {0};

    Queue queue = {0};

    song_arena = arena_create(MiB(1), KiB(1));
    ui_arena = arena_create(KiB(64), KiB(64));

    vectorInit(&songs, sizeof(Song), 32);
    vectorInit(&covers_textures, sizeof(Texture2D), 32);

    queue.songs=&songs;
    queue.current_index_selected=0;

    ctx.queue = &queue;
    ctx.state = STOPPED;

    FilePathList music_files = LoadDirectoryFilesEx("/home/zach/.pymusicterm/musics/", ".mp3", false);

    for (size_t i = 0; i < music_files.count; i++)
    {
        char* filepath = music_files.paths[i];
        Song* song = createSong(song_arena, filepath);
        Texture2D tex = texture_from_image_buffer(song->img);
        vectorAppend(&covers_textures, &tex);
        if (song){
            addSongToQueue(&queue, song, queue.songs->count);
        } else {
            TraceLog(LOG_ERROR, "Failed to create song at %s.", filepath);
        }
    }

    while (!WindowShouldClose())
    {
        double_t percent = (double_t)song_arena->pos / song_arena->reserve_size;
        printf("SONG ARENA: %lu/%lu : %lf\n", song_arena->pos, song_arena->reserve_size, percent);
        percent = (double_t)ui_arena->pos/ui_arena->reserve_size;
        printf("UI ARENA: %lu/%lu : %lf\n", ui_arena->pos, ui_arena->reserve_size, percent);
        if (IsKeyPressed(KEY_S)) selectIndex(&queue, queue.current_index_selected + 1);
        if (IsKeyPressed(KEY_W)) selectIndex(&queue, queue.current_index_selected - 1);
        if (IsKeyPressed(KEY_R)) removeSongFromQueue(&queue, queue.current_index_selected);
        if (IsKeyPressed(KEY_ENTER)) playIndex(&queue, queue.current_index_selected);
        if (IsKeyPressed(KEY_SPACE) && IsMusicValid(music)) { if (IsMusicStreamPlaying(music)) {
                PauseMusicStream(music);
            } else {
                ResumeMusicStream(music);
            }
        }

        if (reinitializeClay) {
            printf("REINITIALIZED\n");
            Clay_SetMaxElementCount(8192);
            totalMemorySize = Clay_MinMemorySize();
            clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));
            Clay_Initialize(clayMemory, (Clay_Dimensions) { (float)GetScreenWidth(), (float)GetScreenHeight() }, (Clay_ErrorHandler) { HandleClayErrors, 0 });
            Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);
            reinitializeClay = false;
        }

        // DEBUG MODE
        if (IsKeyPressed(KEY_D)) {
            debugEnabled = !debugEnabled;
            Clay_SetDebugModeEnabled(debugEnabled);
        }

        //GET DEFAULT DATA FOR MOUSE AND DIMENSION
        Vector2 mouseWheelDelta = GetMouseWheelMoveV();
        float mouseWheelX = mouseWheelDelta.x;
        float mouseWheelY = mouseWheelDelta.y;
        Clay_Vector2 mousePosition = RAYLIB_VECTOR2_TO_CLAY_VECTOR2(GetMousePosition());
        Clay_SetPointerState(mousePosition, IsMouseButtonDown(0) && !scrollbarData.mouseDown);
        Clay_SetLayoutDimensions((Clay_Dimensions) { (float)GetScreenWidth(), (float)GetScreenHeight() });

        // UPDATE THE SCROLL ACCORDINGLY
        Clay_UpdateScrollContainers(true, (Clay_Vector2) {mouseWheelX, mouseWheelY}, GetFrameTime());

        if (IsMusicValid(music)) UpdateMusicStream(music);

        if (IsMusicValid(music) && !music_slider.active) {
            float length = GetMusicTimeLength(music);
            if (length > 0.0f) {
                music_slider.value = GetMusicTimePlayed(music) / length;

                if (music_slider.value < 0.f) music_slider.value = 0.f;
                if (music_slider.value > 1.f) music_slider.value = 1.f;
            }
        }

        // START OF THE LAYOUT
        Clay_BeginLayout();
        CLAY(CLAY_ID("WINDOW"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = COLOR_BACKGROUND}) {
            CLAY(CLAY_ID("PLAYLIST_CONTAINER"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }}) {
                for (size_t i = 0; i < queue.songs->count; i++) {
                    Song song = *(Song*)vectorGet(&songs, i);
                    RenderSong(song, i);
                }
            }
            if (IsMusicValid(music)) {
                Song song = *(Song*)vectorGet(&songs, queue.current_index_playing);
                CLAY(CLAY_ID("PLAYER"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .x = CLAY_ALIGN_X_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_PERCENT(0.3)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                    Clay_String string_title = { .chars = song.title, .length = strlen(song.title), .isStaticallyAllocated = false };
                    CLAY_TEXT(string_title, CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));
                    Clay_String string_artists = { .chars = song.artists, .length = strlen(song.artists), .isStaticallyAllocated = false };
                    CLAY_TEXT(string_artists, CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));
                    CLAY(CLAY_ID("SLIDER_FRAME"), { .layout = { .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .childGap = 8, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {

                        UiTimeString time_played = MakeTimeStringFromFloat(ui_arena, GetMusicTimePlayed(music));
                        CLAY_TEXT(time_played.clay, CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));

                        CLAY(CLAY_ID("SLIDER"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(20)}, .padding = CLAY_PADDING_ALL(2) }, .backgroundColor = COLOR_BACKGROUND}) {
                            Clay_OnHover(HandleSliderInteraction, NULL);
                            float displayValue = (IsMusicValid(music) && music_slider.value >= 0.f && music_slider.value <= 1.f) ? music_slider.value : 0.f;
                            CLAY(CLAY_ID("SLIDER_WIDGET"), { .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(displayValue), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_BLUE});
                        }

                        UiTimeString time_duration = MakeTimeStringFromFloat(ui_arena, song.length);
                        CLAY_TEXT(time_duration.clay, CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));
                    }
                }
            }
        }

        // All clay layouts are declared between Clay_BeginLayout and Clay_EndLayout
        Clay_RenderCommandArray renderCommands = Clay_EndLayout();

        // RAYLIB drawing of the render commands
        BeginDrawing();
        ClearBackground(BLACK);
        Clay_Raylib_Render(renderCommands, fonts);
        EndDrawing();

        // RESET UI ARENA FOR THE NEXT PASS
        arena_clear(ui_arena);
    }

    for (size_t i = 0; i < covers_textures.count; i++) {
        Texture2D* t = vectorGet(&covers_textures, i);
        UnloadTexture(*t);
    }

    UnloadMusicStream(music);
    UnloadFont(fonts[0]);
    vectorFree(&songs);
    vectorFree(&covers_textures);
    arena_destroy(song_arena);
    arena_destroy(ui_arena);
    UnloadDirectoryFiles(music_files);
    CloseAudioDevice();
    Clay_Raylib_Close();
    return 0;
}

Song* createSong(mem_arena* arena, char* filepath)
{
    if (FileExists(filepath))
    {
        Music music = LoadMusicStream(filepath);
        Metadata* metadata = get_metadata_from_mp3(arena, filepath);
        Song* song = PUSH_STRUCT(arena, Song);
        song->title = arena_strdup(arena, metadata->title_text);
        song->artists = arena_strdup(arena, metadata->artist_text);
        song->album = arena_strdup(arena, metadata->album_text);
        song->img = metadata->image;
        song->path = arena_strdup(arena, filepath);
        song->length = GetMusicTimeLength(music);
        UnloadMusicStream(music);
        return song;
    }
    return NULL;
}


bool addSongToQueue(Queue *queue, Song *song, int index)
{
    if(vectorInsert(queue->songs, index, song) < 0) {
        TraceLog(LOG_ERROR, "Inserting the song in the queue failed.");
        return false;
    }
    TraceLog(LOG_INFO, "Title: %s, Artists: %s, Album: %s, Path: %s, Cover type: %s", song->title, song->artists, song->album, song->path, song->img->mime_type);
    return true;
}

Texture2D texture_from_image_buffer(ImageBuffer* img)
{
    if (!img || !img->data || img->size == 0)
        return (Texture2D){0};

    const char* ext = get_file_extension_from_mime(img->mime_type);
    printf("%s\n", ext);
    Image image = LoadImageFromMemory(
        ext,
        img->data,
        (int)img->size
    );

    if (image.data == NULL)
    {
        TraceLog(LOG_ERROR, "Failed to load image from memory");
        return (Texture2D){0};
    }

    Texture2D tex = LoadTextureFromImage(image);
    UnloadImage(image);

    return tex;
}


bool removeSongFromQueue(Queue *queue, int index)
{
    if(vectorRemove(queue->songs, index) < 0) {
        TraceLog(LOG_ERROR, "Removing the song in the queue failed at index %d.", index);
        return false;
    }
    selectIndex(queue, queue->current_index_selected - 1);
    return true;
}

void selectIndex(Queue* queue, int index)
{
    if (index < 0) index = queue->songs->count - 1;
    if ((size_t) index >= queue->songs->count) index = 0;
    queue->current_index_selected = index;
    TraceLog(LOG_INFO, "Selecting index at index %d.", index);

}

void playIndex(Queue* queue, int index)
{
    if (IsMusicValid(music)) UnloadMusicStream(music);
    music = LoadMusicStream(((Song*)vectorGet(queue->songs, index))->path);
    PlayMusicStream(music);
    queue->current_index_playing = index;
}

void HandleClayErrors(Clay_ErrorData errorData)
{
    printf("%s", errorData.errorText.chars);
    if (errorData.errorType == CLAY_ERROR_TYPE_ELEMENTS_CAPACITY_EXCEEDED) {
        reinitializeClay = true;
        Clay_SetMaxElementCount(Clay_GetMaxElementCount() * 2);
    } else if (errorData.errorType == CLAY_ERROR_TYPE_TEXT_MEASUREMENT_CAPACITY_EXCEEDED) {
        reinitializeClay = true;
        Clay_SetMaxMeasureTextCacheWordCount(Clay_GetMaxMeasureTextCacheWordCount() * 2);
    }
}
