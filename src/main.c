#define ARENA_IMPLEMENTATION
#include "utils/arena.h"

#include "core/core.h"
#include "core/queue.h"
#include "raylib.h"
#include <raymath.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "ctype.h"
#include <assert.h>

#include "multi_font/multi_font.c"
#define CLAY_IMPLEMENTATION
#include "../external/clay.h"
#include "../external/raylib_renderer.c"

#define VECTOR_IMPLEMENTATION
#include "utils/vector.h"

#include "command.c"
#include "metadata.c"

#include "core/core.c"
#include "core/audio_raylib.c"

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600

#include "assets/next.h"
#include "assets/pause.h"
#include "assets/play.h"
#include "assets/previous.h"
#include "assets/shuffle.h"
#include "assets/repeat.h"
#include "assets/repeat_on.h"
#include "assets/repeat_on_one.h"
#include "assets/shuffle_on.h"

#define COLOR_BACKGROUND        (Clay_Color){16, 14, 10, 255}
#define COLOR_BACKGROUND_LIGHT  (Clay_Color){26, 23, 18, 255}
#define COLOR_BACKGROUND_DARK   (Clay_Color){11, 10, 7, 255}

#define COLOR_TEXT_PRIMARY      (Clay_Color){238, 232, 226, 255}
#define COLOR_TEXT_SECONDARY    (Clay_Color){176, 170, 164, 255}
#define COLOR_TEXT_MUTED        (Clay_Color){130, 125, 120, 255}

#define COLOR_PRIMARY           (Clay_Color){235, 161, 5, 255}
#define COLOR_SECONDARY         (Clay_Color){201, 140, 12, 255}
#define COLOR_ACCENT            (Clay_Color){170, 120, 45, 255}

#define COLOR_HOVER COLOR_BACKGROUND_DARK

#define FONT_SIZE 16
#define TEXT_CONFIG_24 CLAY_TEXT_CONFIG({ .fontSize = FONT_SIZE, .textColor = COLOR_TEXT_SECONDARY })
#define TEXT_CONFIG_24_BOLD CLAY_TEXT_CONFIG({ .fontId = 1, .fontSize = FONT_SIZE, .textColor = COLOR_TEXT_SECONDARY })

#define RAYLIB_VECTOR2_TO_CLAY_VECTOR2(vector) (Clay_Vector2) { .x = vector.x, .y = vector.y }
#define CLAY_COLOR_TO_RAYLIB_COLOR(color) (Color) { .r = (unsigned char)roundf(color.r), .g = (unsigned char)roundf(color.g), .b = (unsigned char)roundf(color.b), .a = (unsigned char)roundf(color.a) }

typedef struct {
    bool active;           // is dragging
    float value;           // normalized (0 to 1)
    float target_value;    // where we seeked to
    bool has_target;       // true if waiting for seek to complete
} SliderState;

typedef enum {
    TABS_QUEUE,
    TABS_SEARCH
} Tabs;

typedef struct {
    bool is_file_dropped;
    FilePathList files;
    Core* core;
    mem_arena* song_arena;
    mem_arena* string_arena;
    mem_arena* scratch_arena;
    Vector* covers_textures;
    int* next_song_id;
} LoadSongsThreadData;

typedef struct PendingTexture {
    Image image;
    int textureIndex;
} PendingTexture;

typedef struct {
    char** paths;
    int count;
} LoadSongsJob;

extern Vector pending_textures;
extern Vector covers_textures;
extern pthread_mutex_t arena_mutex;

Vector pending_textures = {0};

Texture2D texture2DFromImageBuffer(ImageBuffer* img);
void renderSong(Song* song);
void renderSearchResult(SearchResult* result, int index);
Song* createSong(char* filepath);
bool songMatchesSearch(Song* song, const char* query);
Clay_String timeStringFromFloat(mem_arena* arena, float seconds);
Texture2D createTextureFromMemory(unsigned char* data, int format, int width, int height);
void togglePlayPause(void);

// CLAY RENDER
void HandleClayErrors(Clay_ErrorData errorData);
bool reinitializeClay = false;
bool clayDebugEnabled = false;

void HandleSongInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleSliderInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandlePlayInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleNextInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleShuffleInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandlePreviousInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleSelecTabInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleLoopInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleSearchInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleSearchResultInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);

// Player
Music music = {0};

Vector covers_textures = {0};
mem_arena* song_arena = {0};
mem_arena* ui_arena = {0};
mem_arena* string_arena = {0};
mem_arena* scratch_arena = {0};

SliderState music_slider = {0};
bool debugEnabled = false;
Tabs currentTab = TABS_QUEUE;
static int next_song_id = 0;
static Core *core = {0};

static bool pointer_dragging = false;
static Clay_Vector2 pointer_press_pos;
#define DRAG_THRESHOLD 6.0f

// UI
#define MAX_SEARCH_LENGTH 1024
char searchQuery[MAX_SEARCH_LENGTH] = {0};
bool searchBarActive = false;

const char* working_path = ".";

YoutubeSearch* yt_search = NULL;
YoutubeDownload* yt_download = NULL;

SearchResults* currentSearchResults = NULL;
mem_arena* search_arena = NULL;

int32_t current_cursor = MOUSE_CURSOR_DEFAULT;

pthread_t load_songs_thread = 0;
bool loading_songs = false;
pthread_mutex_t song_loading_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t arena_mutex = PTHREAD_MUTEX_INITIALIZER;

static Font fonts[2] = {0};
static volatile bool fonts_need_rebuild = false;


void print_help(char* program_name) {
    printf("%s: [folder]\n", program_name);
    printf("    PATH TO FOLDER TO SCAN\n");
    exit(-1);
}

const char* preferred[] = { "Noto Sans", "Noto Sans CJK SC" };

void RebuildFonts(void) {
    TraceLog(LOG_INFO, "FONTS: rebuilding atlas from %zu songs...", core_get_queue_count(core));

    // Collect all strings
    size_t song_count = core_get_queue_count(core);
    const char** all_strings = malloc(sizeof(char*) * song_count * 3 + 1);
    int str_count = 0;

    for (size_t i = 0; i < song_count; i++) {
        Song* s = core_get_song_at(core, i);
        all_strings[str_count++] = arena_get_string(string_arena, s->title);
        all_strings[str_count++] = arena_get_string(string_arena, s->artists);
        all_strings[str_count++] = arena_get_string(string_arena, s->album);
    }

    int cp_count = 0;
    int* codepoints = CollectCodepoints(all_strings, str_count, &cp_count);
    free(all_strings);

    TraceLog(LOG_INFO, "FONTS: %d unique codepoints collected", cp_count);

    // Unload old fonts if they exist
    if (fonts[0].texture.id != 0) UnloadFont(fonts[0]);
    if (fonts[1].texture.id != 0) UnloadFont(fonts[1]);

    fonts[0] = BuildMultiFontAtlas("Poppins",           preferred, 2, FONT_SIZE, codepoints, cp_count);
    fonts[1] = BuildMultiFontAtlas("Poppins SemiBold",  preferred, 2, FONT_SIZE, codepoints, cp_count);

    Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);

    TraceLog(LOG_INFO, "FONTS: atlas rebuilt OK");
}

void add_song_from_path(FilePathList files) {
    double start_time = GetTime();
    for (size_t i = 0; i < files.count; i++)
    {
        char* filepath = files.paths[i];
        Song* song = createSong(filepath);
        if (song){
            core_send_command(core, (CoreCommand){ .type = CMD_QUEUE_ADD, .song = song });
        } else {
            TraceLog(LOG_ERROR, "Failed to create song at %s.", filepath);
        }
    }
    TraceLog(LOG_INFO, "Current queue size: %d", core_get_queue_count(core));
    TraceLog(LOG_ERROR, "Loading took: %f", GetTime() - start_time);
}


void processPendingTextures(void)
{
    pthread_mutex_lock(&arena_mutex);

    for (size_t i = 0; i < pending_textures.count; ++i)
    {
        PendingTexture* p = vectorGet(&pending_textures, i);

        Texture2D tex = LoadTextureFromImage(p->image);
        UnloadImage(p->image);

        Texture2D* slot = vectorGet(&covers_textures, p->textureIndex);
        if (slot)
            *slot = tex;
    }

    pending_textures.count = 0;

    pthread_mutex_unlock(&arena_mutex);
}

void* load_songs_thread_func(void* arg) {
    LoadSongsThreadData* data = (LoadSongsThreadData*)arg;

    double start_time = GetTime();
    TraceLog(LOG_INFO, "Starting threaded song loading for %zu files", data->files.count);

    for (size_t i = 0; i < data->files.count; i++) {
        char* filepath = data->files.paths[i];
        Song* song = createSong(filepath);
        if (song) {
            core_send_command(data->core, (CoreCommand){ .type = CMD_QUEUE_ADD, .song = song });
        } else {
            TraceLog(LOG_ERROR, "Failed to create song at %s.", filepath);
        }

        pthread_mutex_lock(&song_loading_mutex);
        fonts_need_rebuild = true;
        pthread_mutex_unlock(&song_loading_mutex);
    }

    TraceLog(LOG_INFO, "Thread finished. Queue: %d, took: %f s",
             core_get_queue_count(data->core), GetTime() - start_time);

    // Final rebuild to catch the last batch
    pthread_mutex_lock(&song_loading_mutex);
    loading_songs = false;
    fonts_need_rebuild = true;
    pthread_mutex_unlock(&song_loading_mutex);

    if (data->is_file_dropped) UnloadDroppedFiles(data->files);
    else                       UnloadDirectoryFiles(data->files);
    free(data);
    return NULL;
}

void start_loading_songs_async(FilePathList files, bool is_file_dropped) {
    pthread_mutex_lock(&song_loading_mutex);
    if (loading_songs) {
        pthread_mutex_unlock(&song_loading_mutex);
        TraceLog(LOG_WARNING, "Already loading songs, ignoring new request");
        return;
    }
    loading_songs = true;
    pthread_mutex_unlock(&song_loading_mutex);

    LoadSongsThreadData* data = malloc(sizeof(LoadSongsThreadData));
    data->files = files;
    data->is_file_dropped = is_file_dropped;
    data->core = core;
    data->song_arena = song_arena;
    data->string_arena = string_arena;
    data->scratch_arena = scratch_arena;
    data->covers_textures = &covers_textures;
    data->next_song_id = &next_song_id;

    if (pthread_create(&load_songs_thread, NULL, load_songs_thread_func, data) != 0) {
        TraceLog(LOG_ERROR, "Failed to create song loading thread");
        pthread_mutex_lock(&song_loading_mutex);
        loading_songs = false;
        pthread_mutex_unlock(&song_loading_mutex);
        UnloadDirectoryFiles(files);
        free(data);
    }

    // Detach the thread so it cleans up automatically
    pthread_detach(load_songs_thread);
}


int main(int argc, char** argv) {
    if (argc > 1) working_path = argv[1];
    if (!DirectoryExists(working_path)) {
        print_help(argv[0]);
        exit(-1);
    }

    // Clay initialisation
    int totalMemorySize = Clay_MinMemorySize();
    Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));
    Clay_Initialize(clayMemory, (Clay_Dimensions) { (float)DEFAULT_WIDTH, (float)DEFAULT_HEIGHT }, (Clay_ErrorHandler) { HandleClayErrors, 0 });

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "Saturn Player");
    InitAudioDevice();

    song_arena = arena_create(MiB(64), MiB(1));
    ui_arena = arena_create(KiB(64), KiB(1));
    search_arena = arena_create(MiB(4), KiB(1));
    string_arena = arena_create(MiB(64), MiB(1));
    scratch_arena = arena_create(MiB(8), MiB(1));

    vectorInit(&covers_textures, sizeof(Texture2D), 1024);
    vectorInit(&pending_textures, sizeof(PendingTexture), 1024);

    AudioBackend *audio = raylib_audio_backend_create();
    core = core_create(audio, string_arena);

    core_start(core);

    Texture2D next_tex = createTextureFromMemory(NEXT_DATA, NEXT_FORMAT, NEXT_WIDTH, NEXT_HEIGHT);
    Texture2D previous_tex = createTextureFromMemory(PREVIOUS_DATA, PREVIOUS_FORMAT, PREVIOUS_WIDTH, PREVIOUS_HEIGHT);
    Texture2D play_tex = createTextureFromMemory(PLAY_DATA, PLAY_FORMAT, PLAY_WIDTH, PLAY_HEIGHT);
    Texture2D pause_tex = createTextureFromMemory(PAUSE_DATA, PAUSE_FORMAT, PAUSE_WIDTH, PAUSE_HEIGHT);
    Texture2D shuffle_tex = createTextureFromMemory(SHUFFLE_DATA, SHUFFLE_FORMAT, SHUFFLE_WIDTH, SHUFFLE_HEIGHT);
    Texture2D shuffle_on_tex = createTextureFromMemory(SHUFFLE_ON_DATA, SHUFFLE_ON_FORMAT, SHUFFLE_ON_WIDTH, SHUFFLE_ON_HEIGHT);
    Texture2D repeat_tex = createTextureFromMemory(REPEAT_DATA, REPEAT_FORMAT, REPEAT_WIDTH, REPEAT_HEIGHT);
    Texture2D repeat_on_tex = createTextureFromMemory(REPEAT_ON_DATA, REPEAT_ON_FORMAT, REPEAT_ON_WIDTH, REPEAT_ON_HEIGHT);
    Texture2D repeat_on_one_tex = createTextureFromMemory(REPEAT_ON_ONE_DATA, REPEAT_ON_ONE_FORMAT, REPEAT_ON_ONE_WIDTH, REPEAT_ON_ONE_HEIGHT);

    Image icon = LoadImageFromTexture(play_tex);
    SetWindowIcon(icon);
    UnloadImage(icon);

    int bootstrap_cps[95];
    for (int i = 0; i < 95; i++) bootstrap_cps[i] = 0x20 + i;
    fonts[0] = BuildMultiFontAtlas("Poppins",           preferred, 2, FONT_SIZE, bootstrap_cps, 95);
    fonts[1] = BuildMultiFontAtlas("Poppins SemiBold",  preferred, 2, FONT_SIZE, bootstrap_cps, 95);
    Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);

    for (int i = 0; i < argc; i++) TraceLog(LOG_INFO, "Arg%d: %s", i, argv[i]);

    if (argc > 1 || IsPathFile(argv[1])) {
        working_path = argv[1];
    } else {
        working_path = ".";
    }

    TraceLog(LOG_WARNING, "Current path: %s", working_path);

    FilePathList music_files = LoadDirectoryFiles(working_path);
    start_loading_songs_async(music_files, false);

    while (!WindowShouldClose())
    {
        current_cursor = MOUSE_CURSOR_DEFAULT;
        if (searchBarActive) {
            int key = GetCharPressed();

            while (key > 0)
            {
                int byteSize = 0;
                const char *utf8 = CodepointToUTF8(key, &byteSize);

                int length = TextLength(searchQuery);

                if (length + byteSize < MAX_SEARCH_LENGTH - 1)
                {
                    memcpy(searchQuery + length, utf8, byteSize);
                    searchQuery[length + byteSize] = '\0';
                }

                key = GetCharPressed();
            }

            if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
            {
                int length = TextLength(searchQuery);

                if (length > 0) {
                    int cpSize = 0;
                    const char *ptr = searchQuery + length;

                    if (IsKeyDown(KEY_LEFT_CONTROL)) {
                        while (length > 0) {
                            int cp = GetCodepointPrevious(ptr, &cpSize);

                            if (cp == ' ') {
                                length -= cpSize;
                                ptr -= cpSize;
                            } else { break; }
                        }

                        while (length > 0) {
                            int cp = GetCodepointPrevious(ptr, &cpSize);

                            if (cp != ' ') {
                                length -= cpSize;
                                ptr -= cpSize;
                            }  else { break; }
                        }

                        searchQuery[length] = '\0';
                    } else {
                        GetCodepointPrevious(ptr, &cpSize);
                        searchQuery[length - cpSize] = '\0';
                    }
                }
            }


            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_TAB)) {
                searchBarActive = false;
            }

        } else {
            if (IsKeyPressed(KEY_S) || IsKeyPressedRepeat(KEY_S)) core_send_command(core, (CoreCommand){ .type = CMD_SELECT_NEXT });
            if (IsKeyPressed(KEY_W) || IsKeyPressedRepeat(KEY_W)) core_send_command(core, (CoreCommand){ .type = CMD_SELECT_PREV });
            //if (IsKeyPressed(KEY_R)) core_send_command(core, (CoreCommand){ .type = CMD_QUEUE_REMOVE });
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressedRepeat(KEY_ENTER)) core_send_command(core, (CoreCommand){ .type = CMD_PLAY_SELECTED });
            if (IsKeyPressed(KEY_D) || IsKeyPressedRepeat(KEY_D)) core_send_command(core, (CoreCommand){ .type = CMD_SEEK_REL, .seek_seconds = 5 });
            if (IsKeyPressed(KEY_A) || IsKeyPressedRepeat(KEY_A)) core_send_command(core, (CoreCommand){ .type = CMD_SEEK_REL, .seek_seconds = -5 });
            if (IsKeyPressed(KEY_SPACE) || IsKeyPressedRepeat(KEY_SPACE)) togglePlayPause();

            if (IsKeyPressed(KEY_H)) {
                clayDebugEnabled = !clayDebugEnabled;
                Clay_SetDebugModeEnabled(clayDebugEnabled);
            }

            if (IsKeyPressed(KEY_F3)) {
                debugEnabled = !debugEnabled;
            }
        }

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_F)) {
            searchBarActive = !searchBarActive;
        }

        if (IsKeyPressed(KEY_ENTER) && searchBarActive && strlen(searchQuery) > 0) {
            if (!yt_search) {
                arena_clear(search_arena);
                yt_search = youtube_search(search_arena, searchQuery, 10);
                currentTab = TABS_SEARCH;
            }
        }

        if (yt_search && youtube_search_done(yt_search)) {
            currentSearchResults = youtube_search_results(yt_search);
            yt_search = NULL;
        }

        if (yt_download && youtube_download_done(yt_download)) {
            TraceLog(LOG_INFO, "Successfuly downloaded %s to %s.", yt_download->url, yt_download->final_path);
            Song* song = createSong(yt_download->final_path);
            core_send_command(core, (CoreCommand) { .type = CMD_QUEUE_ADD, .song = song});
            core_send_command(core, (CoreCommand) { .type = CMD_PLAY_SONG, .song = song});
            yt_download = NULL;
        }

        if (reinitializeClay) {
            totalMemorySize = Clay_MinMemorySize();
            clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));
            Clay_Initialize(clayMemory, (Clay_Dimensions) { (float)GetScreenWidth(), (float)GetScreenHeight() }, (Clay_ErrorHandler) { HandleClayErrors, 0 });
            Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);
            reinitializeClay = false;
        }

        if (IsFileDropped()) {
            TraceLog(LOG_INFO, "FILE DROPPED !");
            FilePathList droppedFiles = LoadDroppedFiles();
            start_loading_songs_async(droppedFiles, true);
            TraceLog(LOG_ERROR, "Current song loaded: %ul.", core_get_queue_count(core));
        }

        processPendingTextures();

        pthread_mutex_lock(&song_loading_mutex);
        bool do_rebuild = fonts_need_rebuild;
        if (do_rebuild) fonts_need_rebuild = false;
        pthread_mutex_unlock(&song_loading_mutex);

        if (do_rebuild) {
            // Also force Clay to re-measure everything
            RebuildFonts();
            reinitializeClay = true;
        }

        //GET DEFAULT DATA FOR MOUSE AND DIMENSION
        Vector2 mouseWheelDelta = GetMouseWheelMoveV();
        if (!Vector2Equals(mouseWheelDelta, Vector2Zero())) {
            searchBarActive = false;
        }

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            pointer_press_pos = RAYLIB_VECTOR2_TO_CLAY_VECTOR2(GetMousePosition());
            pointer_dragging = false;
        }

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            Clay_Vector2 cur = RAYLIB_VECTOR2_TO_CLAY_VECTOR2(GetMousePosition());
            float dx = cur.x - pointer_press_pos.x;
            float dy = cur.y - pointer_press_pos.y;
            if ((dx*dx + dy*dy) > (DRAG_THRESHOLD * DRAG_THRESHOLD)) {
                pointer_dragging = true;
            }
        }

        float mouseWheelX = mouseWheelDelta.x * 3;
        float mouseWheelY = mouseWheelDelta.y * 3;
        Clay_Vector2 mousePosition = RAYLIB_VECTOR2_TO_CLAY_VECTOR2(GetMousePosition());
        Clay_SetPointerState(mousePosition, IsMouseButtonDown(0));
        Clay_SetLayoutDimensions((Clay_Dimensions) { (float)GetScreenWidth(), (float)GetScreenHeight() });

        // UPDATE THE SCROLL ACCORDINGLY
        Clay_UpdateScrollContainers(true, (Clay_Vector2) {mouseWheelX, mouseWheelY}, GetFrameTime());

        if (core_get_current_song_playing(core) && !music_slider.active) {
            Song* song = core_get_current_song_playing(core);
            float length = song->length;
            if (length > 0.0f) {
                float actual_pos = core->audio->vtable->position(audio) / length;
                if (music_slider.has_target) {
                    if (fabsf(actual_pos - music_slider.target_value) < 0.01f) {
                        music_slider.has_target = false;
                        music_slider.value = actual_pos;
                    } else {
                        // Keep showing target position until seek completes
                        music_slider.value = music_slider.target_value;
                    }
                } else {
                    music_slider.value = actual_pos;
                }
                if (music_slider.value < 0.f) music_slider.value = 0.f;
                if (music_slider.value > 1.f) music_slider.value = 1.f;
            }
        }

        // START OF THE LAYOUT
        Clay_BeginLayout();
        CLAY(CLAY_ID("WINDOW"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = COLOR_BACKGROUND_DARK}) {

            CLAY(CLAY_ID("TABS_CONTAINER"), { .layout = { .layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(80) }, .padding = CLAY_PADDING_ALL(16), .childGap = 16}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                    CLAY(CLAY_ID("TABS_QUEUE"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_PERCENT(0.33)}}, .backgroundColor = (currentTab == TABS_QUEUE) ? COLOR_SECONDARY : Clay_Hovered() ? COLOR_BACKGROUND_DARK : COLOR_BACKGROUND}) {
                    Clay_OnHover(HandleSelecTabInteraction, (void *)(uintptr_t)TABS_QUEUE);
                    CLAY_TEXT(CLAY_STRING("QUEUE"), CLAY_TEXT_CONFIG({ .fontId = 1, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = FONT_SIZE, .textColor = COLOR_TEXT_SECONDARY }));
                }
                CLAY(CLAY_ID("TABS_SEARCH"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_PERCENT(0.33)}}, .backgroundColor = (currentTab == TABS_SEARCH) ? COLOR_SECONDARY : Clay_Hovered() ? COLOR_BACKGROUND_DARK : COLOR_BACKGROUND}) {
                    Clay_OnHover(HandleSelecTabInteraction, (void *)(uintptr_t)TABS_SEARCH);
                    CLAY_TEXT(CLAY_STRING("SEARCH"), CLAY_TEXT_CONFIG({ .fontId = 1, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = FONT_SIZE, .textColor = COLOR_TEXT_SECONDARY }));
                }
                CLAY(CLAY_ID("SEARCH_BAR"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_PERCENT(0.33)}, .padding = CLAY_PADDING_ALL(8) }, .clip = { .horizontal = true, .vertical = true }, .backgroundColor = searchBarActive ? COLOR_BACKGROUND_DARK : COLOR_BACKGROUND }) {
                    Clay_OnHover(HandleSearchInteraction, NULL);
                    if (strlen(searchQuery) > 0 || searchBarActive) {
                        char* displayText = arena_push(ui_arena, MAX_SEARCH_LENGTH + 10, false);
                        snprintf(displayText, MAX_SEARCH_LENGTH + 10, "%s%s",
                                searchQuery,
                                searchBarActive ? "_" : "");

                        Clay_String searchText = {
                            .chars = displayText,
                            .length = strlen(displayText),
                            .isStaticallyAllocated = false
                        };
                        CLAY_TEXT(searchText, TEXT_CONFIG_24);
                    } else {
                        CLAY_TEXT(CLAY_STRING("Click to search..."), CLAY_TEXT_CONFIG({
                            .fontSize = FONT_SIZE,
                            .textColor = {150,150,150,255}
                        }));
                    }
                }
            }

            if (currentTab == TABS_QUEUE) {
                CLAY(CLAY_ID("QUEUE_CONTAINER"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }, .backgroundColor = COLOR_BACKGROUND}) {
                    size_t matchCount = 0;

                    for (size_t i = 0; i < core_get_queue_count(core); i++) {
                        Song *song = core_get_song_at(core, i);
                        if (songMatchesSearch(song, searchQuery)) {
                            renderSong(song);
                            matchCount++;
                        }
                    }

                    if (matchCount == 0 && strlen(searchQuery) > 0) {
                        CLAY(CLAY_ID("NO_RESULTS"), {
                            .layout = {
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                                .padding = CLAY_PADDING_ALL(32)
                            }
                        }) {
                            CLAY_TEXT(CLAY_STRING("No songs found"), TEXT_CONFIG_24_BOLD);
                        }
                    }

                    // Show loading indicator when songs are being loaded
                    pthread_mutex_lock(&song_loading_mutex);
                    bool is_loading = loading_songs;
                    pthread_mutex_unlock(&song_loading_mutex);

                    if (is_loading && matchCount == 0) {
                        CLAY(CLAY_ID("LOADING"), {
                            .layout = {
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                                .padding = CLAY_PADDING_ALL(32)
                            }
                        }) {
                            CLAY_TEXT(CLAY_STRING("Loading songs..."), TEXT_CONFIG_24_BOLD);
                        }
                    }
                }
            } else if (currentTab == TABS_SEARCH) {
                CLAY(CLAY_ID("SEARCH_CONTAINER"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }, .backgroundColor = COLOR_BACKGROUND }) {
                    if (currentSearchResults && currentSearchResults->count > 0) {
                        for (int i = 0; i < currentSearchResults->count; i++) {
                            renderSearchResult(&currentSearchResults->results[i], i);
                        }
                    } else if (yt_search == NULL) {
                        CLAY(CLAY_ID("NO_RESULTS"), {
                            .layout = {
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                                .padding = CLAY_PADDING_ALL(32)
                            }
                        }) {
                        CLAY_TEXT(CLAY_STRING("Search and Press Enter"), TEXT_CONFIG_24_BOLD);
                        }
                    } else if (!youtube_search_done(yt_search)) {
                        CLAY(CLAY_ID("NO_RESULTS"), {
                            .layout = {
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                                .padding = CLAY_PADDING_ALL(32)
                            }
                        }) {
                        CLAY_TEXT(CLAY_STRING("Searching..."), TEXT_CONFIG_24_BOLD);
                        }
                    } else if (strlen(searchQuery) > 0) {
                        CLAY(CLAY_ID("NO_RESULTS"), {
                            .layout = {
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                                .padding = CLAY_PADDING_ALL(32)
                            }
                        }) {
                        CLAY_TEXT(CLAY_STRING("No results found"), TEXT_CONFIG_24_BOLD);
                        }
                    }
                }
            }

            if (core->audio->vtable->is_loaded(audio)) {
                Song* song = core_get_current_song_playing(core);
                const char* song_title = arena_get_string(string_arena, song->title);
                const char* song_artists = arena_get_string(string_arena, song->artists);

                CLAY(CLAY_ID("PLAYER"), { .clip = { .horizontal = true, .vertical = true }, .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .x = CLAY_ALIGN_X_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(140)}, .padding = CLAY_PADDING_ALL(8), .childGap = 4 }, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                    Clay_String string_title = { .chars = song_title, .length = strlen(song_title), .isStaticallyAllocated = false };
                    CLAY_TEXT(string_title, TEXT_CONFIG_24_BOLD);

                    Clay_String string_artists = { .chars = song_artists, .length = strlen(song_artists), .isStaticallyAllocated = false };
                    CLAY_TEXT(string_artists, TEXT_CONFIG_24);
                    CLAY(CLAY_ID("SLIDER_FRAME"), { .layout = { .padding = { .left = 8, .right = 8 } , .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .childGap = 8, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {

                        Clay_String time_played = timeStringFromFloat(ui_arena, core->audio->vtable->position(core->audio));
                        CLAY_TEXT(time_played, CLAY_TEXT_CONFIG({ .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = FONT_SIZE, .textColor = COLOR_TEXT_SECONDARY }));

                        CLAY(CLAY_ID("SLIDER"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(20)}, .padding = CLAY_PADDING_ALL(2) }, .backgroundColor = COLOR_BACKGROUND}) {
                            Clay_OnHover(HandleSliderInteraction, NULL);
                            float displayValue = (music_slider.value >= 0.f && music_slider.value <= 1.f) ? music_slider.value : 0.f;
                            CLAY(CLAY_ID("SLIDER_WIDGET"), { .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(displayValue), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_SECONDARY}) {}
                        }

                        Clay_String time_duration = timeStringFromFloat(ui_arena, song->length);
                        CLAY_TEXT(time_duration, CLAY_TEXT_CONFIG({ .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = FONT_SIZE, .textColor = COLOR_TEXT_SECONDARY }));
                    }

                    CLAY(CLAY_ID("BUTTON_FRAME"), { .layout = { .padding = { 8, 8, 8, 8} , .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = { .x = CLAY_ALIGN_X_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .childGap = 16}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                        CLAY(CLAY_ID("SHUFFLE_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_FIXED(60)}}}) {
                            Clay_OnHover(HandleShuffleInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = core_is_shuffle_enabled(core) ? &shuffle_on_tex : &shuffle_tex}, .aspectRatio = { 1 }, .backgroundColor = (Clay_Hovered() ? COLOR_TEXT_MUTED : COLOR_TEXT_PRIMARY)}) {}
                        }
                        CLAY(CLAY_ID("PREVIOUS_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_FIXED(60)}}}) {
                            Clay_OnHover(HandlePreviousInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = &previous_tex}, .aspectRatio = { 1 }, .backgroundColor = (Clay_Hovered() ? COLOR_TEXT_MUTED : COLOR_TEXT_PRIMARY)}) {}
                        }
                        CLAY(CLAY_ID("PLAY_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_FIXED(60)}}}) {
                            Clay_OnHover(HandlePlayInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = (core_get_state(core) == CORE_PLAYING) ? &pause_tex : &play_tex}, .aspectRatio = { 1 }, .backgroundColor = (Clay_Hovered() ? COLOR_TEXT_MUTED : COLOR_TEXT_PRIMARY)}) {}
                        }
                        CLAY(CLAY_ID("NEXT_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_FIXED(60)}}}) {
                            Clay_OnHover(HandleNextInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = {.imageData = &next_tex}, .aspectRatio = {1}, .backgroundColor = (Clay_Hovered() ? COLOR_TEXT_MUTED : COLOR_TEXT_PRIMARY)}) {}
                        }
                        CLAY(CLAY_ID("LOOP_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_FIXED(60)}}}) {
                            Clay_OnHover(HandleLoopInteraction, NULL);
                            void* img;
                            switch (core_get_loop_mode(core)) {
                                case LOOP_NONE: img = &repeat_tex; break;
                                case LOOP_ONE: img = &repeat_on_one_tex; break;
                                case LOOP_ALL: img = &repeat_on_tex; break;
                                default: img = &repeat_on_one_tex; break;
                            }
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = img }, .aspectRatio = { 1 },  .backgroundColor = (Clay_Hovered() ? COLOR_TEXT_MUTED : COLOR_TEXT_PRIMARY)}) {}
                        }
                    }
                }
            }
        }

        if (debugEnabled) {
            CLAY(CLAY_ID("DEBUG_PANEL"), { .floating = { .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID, .parentId = CLAY_ID("WINDOW").id }, .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {  .height = CLAY_SIZING_FIT(0), .width = CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(16)}, .backgroundColor = COLOR_PRIMARY}) {
                char* buf = arena_push(ui_arena, 256, false);
                snprintf(buf, 256, "Song ARENA: %lu/%lu = %lf\n", song_arena->pos, song_arena->reserve_size, (double_t)song_arena->pos / song_arena->reserve_size);
                Clay_String string = { .chars = buf, .isStaticallyAllocated = false, .length = strlen(buf)};
                CLAY_TEXT(string , TEXT_CONFIG_24);

                char* buf2 = arena_push(ui_arena, 256, false);
                snprintf(buf2, 256, "Ui ARENA: %lu/%lu = %lf\n", ui_arena->pos, ui_arena->reserve_size, (double_t)ui_arena->pos / ui_arena->reserve_size);
                Clay_String string2 = { .chars = buf2, .isStaticallyAllocated = false, .length = strlen(buf2)};
                CLAY_TEXT(string2 , TEXT_CONFIG_24);

                char* buf3 = arena_push(ui_arena, 256, false);
                snprintf(buf3, 256, "Frame Time: %f\n", GetFrameTime());
                Clay_String string3 = { .chars = buf3, .isStaticallyAllocated = false, .length = strlen(buf3)};
                CLAY_TEXT(string3 , TEXT_CONFIG_24);

                pthread_mutex_lock(&song_loading_mutex);
                bool is_loading = loading_songs;
                pthread_mutex_unlock(&song_loading_mutex);

                char* buf4 = arena_push(ui_arena, 256, false);
                snprintf(buf4, 256, "Loading: %s\n", is_loading ? "Yes" : "No");
                Clay_String string4 = { .chars = buf4, .isStaticallyAllocated = false, .length = strlen(buf4)};
                CLAY_TEXT(string4 , TEXT_CONFIG_24);

                pthread_mutex_lock(&arena_mutex);
                size_t pending_count = pending_textures.count;
                pthread_mutex_unlock(&arena_mutex);

                char* buf5 = arena_push(ui_arena, 256, false);
                snprintf(buf5, 256, "Pending Textures: %zu\n", pending_count);
                Clay_String string5 = { .chars = buf5, .isStaticallyAllocated = false, .length = strlen(buf5)};
                CLAY_TEXT(string5 , TEXT_CONFIG_24);
            }
        }

        Clay_RenderCommandArray renderCommands = Clay_EndLayout();

        BeginDrawing();
        ClearBackground(CLAY_COLOR_TO_RAYLIB_COLOR(COLOR_BACKGROUND));
        Clay_Raylib_Render(renderCommands, fonts);
        if (debugEnabled) DrawFPS(0, 0);
        EndDrawing();

        SetMouseCursor(current_cursor);

        // RESET UI ARENA FOR THE NEXT PASS
        arena_clear(ui_arena);
    }

    // Clean up mutexes
    pthread_mutex_destroy(&song_loading_mutex);
    pthread_mutex_destroy(&arena_mutex);

    for (size_t i = 0; i < covers_textures.count; i++) {
        Texture2D* t = vectorGet(&covers_textures, i);
        UnloadTexture(*t);
    }

    UnloadTexture(play_tex);
    UnloadTexture(next_tex);
    UnloadTexture(previous_tex);
    UnloadTexture(pause_tex);
    UnloadTexture(shuffle_tex);
    UnloadTexture(shuffle_on_tex);
    UnloadTexture(repeat_tex);
    UnloadTexture(repeat_on_tex);
    UnloadTexture(repeat_on_one_tex);

    core_stop(core);
    core_destroy(core);
    raylib_audio_backend_destroy(audio);

    UnloadFont(fonts[0]);
    UnloadFont(fonts[1]);

    UnloadMusicStream(music);
    vectorFree(&covers_textures);
    vectorFree(&pending_textures);
    arena_destroy(song_arena);
    arena_destroy(ui_arena);
    arena_destroy(scratch_arena);
    arena_destroy(string_arena);
    CloseAudioDevice();
    Clay_Raylib_Close();
    return 0;
}

Song* createSong(char* filepath)
{
    if (!FileExists(filepath))
        return NULL;

    mem_arena* local_scratch = arena_create(MiB(8), MiB(1));
    Metadata* metadata = get_metadata_from_mp3(local_scratch, filepath);

    pthread_mutex_lock(&arena_mutex);
    Music music = LoadMusicStream(filepath);
    float song_length = GetMusicTimeLength(music);
    UnloadMusicStream(music);

    Song* song = PUSH_STRUCT(song_arena, Song);
    memset(song, 0, sizeof(Song));

    song->id = next_song_id++;

    song->title = arena_push_string_id(
        string_arena,
        metadata->title_text ? metadata->title_text : GetFileNameWithoutExt(filepath)
    );

    song->artists = arena_push_string_id(
        string_arena,
        metadata->artist_text ? metadata->artist_text : "Unknown Artist"
    );

    song->album = arena_push_string_id(
        string_arena,
        metadata->album_text ? metadata->album_text : "Unknown Album"
    );

    song->path   = arena_push_string_id(string_arena, filepath);
    song->length = song_length;

    song->textureIndex = covers_textures.count;
    Texture2D placeholder = {0};
    vectorAppend(&covers_textures, &placeholder);

    pthread_mutex_unlock(&arena_mutex);

    if (metadata->image && metadata->image->data && metadata->image->size > 0) {
        const char* ext = get_file_extension_from_mime(metadata->image->mime_type);

        Image img = LoadImageFromMemory(ext, metadata->image->data, (int)metadata->image->size);

        if (img.data) {
            Rectangle rect;
            if (img.width > img.height) {
                float offset = (img.width - img.height) * 0.5f;
                rect = (Rectangle){ offset, 0, img.height, img.height };
            }
            else if (img.height > img.width) {
                float offset = (img.height - img.width) * 0.5f;
                rect = (Rectangle){ 0, offset, img.width, img.width };
            } else {
                rect = (Rectangle){ 0, 0, img.width, img.height };
            }

            ImageCrop(&img, rect);

            PendingTexture pending = {0};
            pending.image = img;
            pending.textureIndex = song->textureIndex;

            pthread_mutex_lock(&arena_mutex);
            vectorAppend(&pending_textures, &pending);
            pthread_mutex_unlock(&arena_mutex);
        }
    }

    arena_destroy(local_scratch);
    return song;
}

Texture2D texture2DFromImageBuffer(ImageBuffer* img)
{
    if (!img || !img->data || img->size == 0)
        return (Texture2D){0};

    const char* ext = get_file_extension_from_mime(img->mime_type);
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

    Rectangle rect;

    if (image.width > image.height) {
        float offset = (image.width - image.height) * 0.5f;
        rect = (Rectangle) { .x = offset, .y = 0, .width = image.height, .height = image.height };

    } else if (image.height > image.width) {
        float offset = (image.height - image.width) * 0.5f;
        rect = (Rectangle) { .x = 0, .y = offset, .width = image.width, .height = image.width} ;
    } else {
        // already square
        rect = (Rectangle) { 0, 0, image.width, image.height };
    }

    ImageCrop(&image, rect);


    Texture2D tex = LoadTextureFromImage(image);
    UnloadImage(image);

    return tex;
}


void HandleClayErrors(Clay_ErrorData errorData)
{
    TraceLog(LOG_ERROR, "CLAY: %s with type: %d", errorData.errorText.chars, errorData.errorType);
    if (errorData.errorType == CLAY_ERROR_TYPE_ELEMENTS_CAPACITY_EXCEEDED) {
        reinitializeClay = true;
        Clay_SetMaxElementCount(Clay_GetMaxElementCount() * 2);
    } else if (errorData.errorType == CLAY_ERROR_TYPE_TEXT_MEASUREMENT_CAPACITY_EXCEEDED) {
        reinitializeClay = true;
        Clay_SetMaxMeasureTextCacheWordCount(Clay_GetMaxMeasureTextCacheWordCount() * 2);
    }
}

void renderSong(Song* song) {
    bool is_selected = (song == core_get_current_song_selected(core));
    bool is_playing = (song == core_get_current_song_playing(core));
    const char* song_title = arena_get_string(string_arena, song->title);
    const char* song_album = arena_get_string(string_arena, song->album);
    const char* song_artists = arena_get_string(string_arena, song->artists);

    CLAY_AUTO_ID({
        .layout = {
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(80)},
            .childGap = 8
        },
        .backgroundColor = is_selected ? COLOR_BACKGROUND_DARK : Clay_Hovered() ? COLOR_BACKGROUND_LIGHT : COLOR_BACKGROUND
    }) {
        Clay_Color color = is_selected ? COLOR_BACKGROUND_DARK : Clay_Hovered() ? COLOR_BACKGROUND_LIGHT : COLOR_BACKGROUND;
        Clay_OnHover(HandleSongInteraction, song);

        // Album cover
        CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(80), .height = CLAY_SIZING_FIXED(80)} }, .image = { .imageData = vectorGet(&covers_textures, song->textureIndex)}, .aspectRatio = { 1 }});

        // Title and artist
        CLAY_AUTO_ID({
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                .padding = {8, 8, 8, 8},
                .childGap = 16
            },
            .backgroundColor = color
        }) {
            Clay_String string_title = { .chars = song_title, .length = strlen(song_title), .isStaticallyAllocated = false };
            CLAY_TEXT(string_title, CLAY_TEXT_CONFIG({ .fontId = 1, .fontSize = FONT_SIZE, .textColor = is_playing ? COLOR_SECONDARY : COLOR_TEXT_PRIMARY }));
            Clay_String string_artists = { .chars = song_artists, .length = strlen(song_artists), .isStaticallyAllocated = false };
            CLAY_TEXT(string_artists, TEXT_CONFIG_24);
        }

        // Album
        CLAY_AUTO_ID({
            .layout = {
                .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER},
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                .padding = {8, 8, 8, 8},
                .childGap = 16
            },
            .backgroundColor = color
        }) {
            Clay_String string_album = { .chars = song_album, .length = strlen(song_album), .isStaticallyAllocated = false };
            CLAY_TEXT(string_album, TEXT_CONFIG_24);
        }

        // Duration
        CLAY_AUTO_ID({
            .layout = {
                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER },
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                .padding = {8, 8, 8, 8},
                .childGap = 16
            },
            .backgroundColor = color
        }) {
            Clay_String time = timeStringFromFloat(ui_arena, song->length);
            CLAY_TEXT(time, TEXT_CONFIG_24);
        }
    }
}

Clay_String timeStringFromFloat(mem_arena* arena, float seconds)
{
    int h, m, s;
    int total = (int)round(seconds);

    h = total / 3600;
    total %= 3600;

    m = total / 60;
    s = total % 60;

    char* buf = arena_push(arena, 16, false);

    if (h == 0) snprintf(buf, 16, "%02d:%02d", m, s);
    else snprintf(buf, 16, "%02d:%02d:%02d", h, m, s);

    return (Clay_String){ .chars = buf, .length = (h==0) ? 6 : 8, .isStaticallyAllocated = false, };
}

void HandleSliderInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;
    (void)userData;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    if (!core->audio->vtable->is_loaded(core->audio))
        return;

    float length = core_get_current_song_playing(core)->length;
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

    if (pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME && mouseOver) {
        music_slider.active = true;
        searchBarActive = false;
    }

    if (music_slider.active &&
        pointerInfo.state == CLAY_POINTER_DATA_PRESSED) {

        float localX = m.x - bb.x;
        music_slider.value = localX / bb.width;

        if (music_slider.value < 0.f) music_slider.value = 0.f;
        if (music_slider.value > 1.f) music_slider.value = 1.f;
    }

    if (music_slider.active &&
        pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME) {
        core_send_command(core, (CoreCommand) { .type = CMD_SEEK_ABS, .seek_seconds = music_slider.value * length});
        music_slider.target_value = music_slider.value;
        music_slider.has_target = true;
        music_slider.active = false;
    }

    if (!music_slider.active) {
        if (music_slider.value < 0.f) music_slider.value = 0.f;
        if (music_slider.value > 1.f) music_slider.value = 1.f;
    }
}

void HandleSearchInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;
    (void)userData;

    current_cursor = MOUSE_CURSOR_IBEAM;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging) {
        searchBarActive = !searchBarActive;
        TraceLog(LOG_INFO, "Search bar active: %d", searchBarActive);
    }
}

void HandleSongInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) elementId;
    Song* song = (Song*)userData;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging) {
        searchBarActive = false;
        Song* curr_selected = core_get_current_song_selected(core);
        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            core_send_command(core, (CoreCommand){.type = CMD_QUEUE_ADD, .song = song});
            return;
        } else if (IsKeyDown(KEY_LEFT_CONTROL)) {
            core_send_command(core, (CoreCommand){.type = CMD_QUEUE_REMOVE, .song = song});
            return;
        } else if (curr_selected->id == song->id) {
            core_send_command(core, (CoreCommand){ .type = CMD_PLAY_SELECTED });
        } else {
            core_send_command(core, (CoreCommand){ .type = CMD_SELECT_SONG, .song = song});
        }
    }
}

void HandleShuffleInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData){
    (void)userData;
    (void) elementId;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        searchBarActive = false;
        core_send_command(core, (CoreCommand){ .type = CMD_TOGGLE_SHUFFLE, .shuffle_enabled = !queue_is_shuffle_enabled(&core->queue)});
    }
}

void HandlePreviousInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) elementId;
    (void)userData;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        searchBarActive = false;
        core_send_command(core, (CoreCommand) { .type = CMD_PLAY_PREV });
    }
}

void HandleNextInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) elementId;
    (void)userData;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        searchBarActive = false;
        core_send_command(core, (CoreCommand) { .type = CMD_PLAY_NEXT });
    }
}

void HandlePlayInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) elementId;
    (void)userData;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        searchBarActive = false;
        if (core_get_state(core) == CORE_PLAYING) {
            core_send_command(core, (CoreCommand) { .type = CMD_PAUSE });
        } else {
            core_send_command(core, (CoreCommand) { .type = CMD_RESUME });
        }
    }
}

void HandleLoopInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;
    (void)userData;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        searchBarActive = false;
        LoopMode curr_mode = core_get_loop_mode(core);
        LoopMode new_mode = LOOP_NONE;
        switch (curr_mode) {
            case LOOP_NONE: new_mode = LOOP_ALL;  break;
            case LOOP_ONE:  new_mode = LOOP_NONE;  break;
            case LOOP_ALL:  new_mode = LOOP_ONE; break;
        }
        core_send_command(core, (CoreCommand) { .type = CMD_SET_LOOP_MODE, .loop_mode = new_mode });
    }
}

void HandleSelecTabInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    Tabs new_tab = (Tabs)(uintptr_t)userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        currentTab = new_tab;
        searchBarActive = false;
        TraceLog(LOG_INFO, "Current tab set to %d", currentTab);
    }
}

void renderSearchResult(SearchResult* result, int index) {
    CLAY_AUTO_ID({
        .layout = {
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(80)},
            .childGap = 8
        },
        .backgroundColor = Clay_Hovered() ? COLOR_BACKGROUND : COLOR_BACKGROUND_LIGHT
    }) {
        Clay_OnHover(HandleSearchResultInteraction, result);

        CLAY_AUTO_ID({
            .layout = {
                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                .sizing = { .width = CLAY_SIZING_FIXED(60), .height = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(8)
            }
        }) {
            char* index_str = arena_push(ui_arena, 16, false);
            snprintf(index_str, 16, "#%d", index + 1);
            Clay_String str = { .chars = index_str, .length = strlen(index_str), .isStaticallyAllocated = false };
            CLAY_TEXT(str, TEXT_CONFIG_24_BOLD);
        }

        CLAY_AUTO_ID({
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(8),
                .childGap = 8
            }
        }) {
            Clay_String title_str = { .chars = result->title, .length = strlen(result->title), .isStaticallyAllocated = false };
            CLAY_TEXT(title_str, TEXT_CONFIG_24_BOLD);

            Clay_String artist_str = { .chars = result->artist, .length = strlen(result->artist), .isStaticallyAllocated = false };
            CLAY_TEXT(artist_str, TEXT_CONFIG_24);
        }

        CLAY_AUTO_ID({
            .layout = {
                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER },
                .sizing = { .width = CLAY_SIZING_FIXED(100), .height = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(8)
            }
        }) {
            Clay_String duration_str = { .chars = result->duration, .length = strlen(result->duration), .isStaticallyAllocated = false };
            CLAY_TEXT(duration_str, TEXT_CONFIG_24);
        }
    }
}

void HandleSearchResultInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    SearchResult* result = (SearchResult*)userData;
    (void)elementId;

    current_cursor = MOUSE_CURSOR_POINTING_HAND;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging) {
        searchBarActive = false;
        yt_download = youtube_download(search_arena, result->url, working_path);
    }
}

Texture2D createTextureFromMemory(unsigned char* data, int format, int width, int height) {
    Image image = {
        .data = data,
        .format = format,
        .height = height,
        .width = width,
        .mipmaps = 1,
    };
    Texture2D img_tex = LoadTextureFromImage(image);
   	SetTextureFilter(img_tex, TEXTURE_FILTER_BILINEAR);
    return img_tex;
}

bool songMatchesSearch(Song* song, const char* query) {
    if (query == NULL || strlen(query) == 0) return true;

    char lowerQuery[MAX_SEARCH_LENGTH];
    char lowerTitle[256];
    char lowerArtist[256];
    char lowerAlbum[256];

    size_t queryLen = strlen(query);
    for (size_t i = 0; i < queryLen && i < MAX_SEARCH_LENGTH - 1; i++) {
        lowerQuery[i] = tolower((unsigned char)query[i]);
    }
    lowerQuery[queryLen] = '\0';


    const char* song_title = arena_get_string(string_arena, song->title);
    const char* song_artists = arena_get_string(string_arena, song->artists);
    const char* song_album = arena_get_string(string_arena, song->album);

    size_t titleLen = strlen(song_title);
    for (size_t i = 0; i < titleLen && i < 255; i++) {
        lowerTitle[i] = tolower((unsigned char)song_title[i]);
    }
    lowerTitle[titleLen] = '\0';

    size_t artistLen = strlen(song_artists);
    for (size_t i = 0; i < artistLen && i < 255; i++) {
        lowerArtist[i] = tolower((unsigned char)song_artists[i]);
    }
    lowerArtist[artistLen] = '\0';

    size_t albumLen = strlen(song_album);
    for (size_t i = 0; i < albumLen && i < 255; i++) {
        lowerAlbum[i] = tolower((unsigned char)song_album[i]);
    }
    lowerAlbum[albumLen] = '\0';

    return (strstr(lowerTitle, lowerQuery) != NULL ||
            strstr(lowerArtist, lowerQuery) != NULL ||
            strstr(lowerAlbum, lowerQuery) != NULL);
}

void togglePlayPause() {
    CorePlaybackState curr_core = core_get_state(core);
    if (curr_core == CORE_PLAYING)
        core_send_command(core, (CoreCommand){ .type = CMD_PAUSE });
    else if (curr_core == CORE_PAUSED)
        core_send_command(core, (CoreCommand){ .type = CMD_RESUME });
}
