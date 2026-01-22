#include "core/core.h"
#include "core/queue.h"
#include "raylib.h"
#include <raymath.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "core/core.c"
#include "core/audio_raylib.c"
#include "ctype.h"

#define CLAY_IMPLEMENTATION
#include "../external/clay.h"
#include "../external/raylib_renderer.c"

#define VECTOR_IMPLEMENTATION
#include "utils/vector.h"
#define ARENA_IMPLEMENTATION
#include "utils/arena.h"

#include "command.c"

#include "metadata.c"

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600

#define COLOR_ORANGE (Clay_Color) {225, 138, 50, 255}
#define COLOR_BLUE (Clay_Color) {111, 173, 162, 255}

#define IMGS_PATH "../assets/imgs/"
#define FONTS_PATH "../assets/fonts/"

#include "assets/Poppins_Regular.h"
#include "assets/Poppins_SemiBold.h"
#include "assets/next.h"
#include "assets/pause.h"
#include "assets/play.h"
#include "assets/previous.h"
#include "assets/shuffle.h"
#include "assets/repeat.h"
#include "assets/repeat_on.h"
#include "assets/repeat_on_one.h"
#include "assets/shuffle_on.h"

#define COLOR_BACKGROUND (Clay_Color) {20, 20, 20, 255}
#define COLOR_BACKGROUND_LIGHT (Clay_Color) {40, 40, 40, 255}
#define TEXT_CONFIG_24 CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} })
#define TEXT_CONFIG_24_BOLD CLAY_TEXT_CONFIG({ .fontId = 1, .fontSize = 24, .textColor = {255,255,255,255} })

#define RAYLIB_VECTOR2_TO_CLAY_VECTOR2(vector) (Clay_Vector2) { .x = vector.x, .y = vector.y }
#define CLAY_COLOR_TO_RAYLIB_COLOR(color) (Color) { .r = (unsigned char)roundf(color.r), .g = (unsigned char)roundf(color.g), .b = (unsigned char)roundf(color.b), .a = (unsigned char)roundf(color.a) }

typedef struct {
    bool active;        // currently dragging
    float value;        // 0..1 normalized
} SliderState;

typedef struct {
    char* buf;
    Clay_String clay;
} UiTimeString;

typedef enum {
    TABS_QUEUE,
    TABS_SEARCH
} Tabs;

Texture2D texture2DFromImageBuffer(ImageBuffer* img);
void renderSong(Song* song);
void renderSearchResult(SearchResult* result, int index);
Song* createSong(mem_arena* arena, char* filepath);
bool songMatchesSearch(Song* song, const char* query);
UiTimeString timeStringFromFloat(mem_arena* arena, float seconds);
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
SliderState music_slider = {0};
bool debugEnabled = false;
Tabs currentTab = TABS_QUEUE;
static int next_song_id = 0;
static Core *core = {0};

static bool pointer_dragging = false;
static Clay_Vector2 pointer_press_pos;
#define DRAG_THRESHOLD 6.0f

// UI
#define MAX_SEARCH_LENGTH 256
char searchQuery[MAX_SEARCH_LENGTH] = {0};
bool searchBarActive = false;
char youtubeSearchQuery[MAX_SEARCH_LENGTH] = {0};

const char* working_path = ".";

YoutubeSearch* yt_search = NULL;
YoutubeDownload* yt_download = NULL;

SearchResults* currentSearchResults = NULL;
mem_arena* search_arena = NULL;

void print_help(char* program_name) {
    printf("%s: [folder]\n", program_name);
    printf("    PATH TO FOLDER TO SCAN\n");
    exit(-1);
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

    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "Music Player");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetWindowState(FLAG_MSAA_4X_HINT);
    SetTargetFPS(240);
    InitAudioDevice();

    vectorInit(&covers_textures, sizeof(Texture2D), 64);

    AudioBackend *audio = raylib_audio_backend_create();
    core = core_create(audio);

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

    Font poppins_regular = LoadFont_Poppins_Regular();
    Font poppins_semibold = LoadFont_Poppins_SemiBold();

    Font fonts[] = {
        poppins_regular,
        poppins_semibold,
    };

   	SetTextureFilter(fonts[0].texture, TEXTURE_FILTER_BILINEAR);
   	SetTextureFilter(fonts[1].texture, TEXTURE_FILTER_BILINEAR);

    Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);

    song_arena = arena_create(MiB(64), KiB(1));
    ui_arena = arena_create(KiB(64), KiB(1));
    search_arena = arena_create(MiB(4), KiB(1));

    for (int i = 0; i < argc; i++) TraceLog(LOG_INFO, "Arg%d: %s", i, argv[i]);


    if (argc > 1 || IsPathFile(argv[1])) {
        working_path = argv[1];
    } else {
        working_path = ".";
    }

    TraceLog(LOG_WARNING, "Current path: %s", working_path);

    FilePathList music_files = LoadDirectoryFilesEx(working_path, ".mp3", false);

    for (size_t i = 0; i < music_files.count; i++)
    {
        char* filepath = music_files.paths[i];
        Song* song = createSong(song_arena, filepath);
        if (song){
            core_send_command(core, (CoreCommand){ .type = CMD_QUEUE_ADD, .song = song });
        } else {
            TraceLog(LOG_ERROR, "Failed to create song at %s.", filepath);
        }
    }

    while (!WindowShouldClose())
    {

        if (searchBarActive) {
            int key = GetCharPressed();
            while (key > 0) {
                if ((key >= 32) && (key <= 125) && (strlen(searchQuery) < MAX_SEARCH_LENGTH - 1)) {
                    size_t len = strlen(searchQuery);
                    searchQuery[len] = (char)key;
                    searchQuery[len + 1] = '\0';
                }
                key = GetCharPressed();
            }

            // TODO: A REFAIRE C'EST MOCHE
            if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
                size_t len = strlen(searchQuery);
                if (IsKeyDown(KEY_LEFT_CONTROL)) {
                    if (len > 0 && searchQuery[len - 1] == ' ') {
                        searchQuery[len - 1] = '\0';
                    } else {
                        for (int i = len; i > 0; i--) {
                            if (searchQuery[i - 1] == ' ') break;
                            searchQuery[i - 1] = '\0';
                        }
                    }
                } else {
                    if (len > 0) {
                        searchQuery[len - 1] = '\0';
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
            }
        }

        if (yt_search && youtube_search_done(yt_search)) {
            currentSearchResults = youtube_search_results(yt_search);
            yt_search = NULL;
        }

        if (yt_download && youtube_download_done(yt_download)) {
            TraceLog(LOG_INFO, "Successfuly downloaded %s to %s.", yt_download->url, yt_download->final_path);
            Song* song = createSong(song_arena, yt_download->final_path);
            core_send_command(core, (CoreCommand) { .type = CMD_QUEUE_ADD, .song = song});
            core_send_command(core, (CoreCommand) { .type = CMD_PLAY_SONG, .song = song});
            yt_download = NULL;
        }

        if (reinitializeClay) {
            Clay_SetMaxElementCount(8192);
            totalMemorySize = Clay_MinMemorySize();
            clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));
            Clay_Initialize(clayMemory, (Clay_Dimensions) { (float)GetScreenWidth(), (float)GetScreenHeight() }, (Clay_ErrorHandler) { HandleClayErrors, 0 });
            Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);
            reinitializeClay = false;
        }

        if (IsFileDropped()) {
            TraceLog(LOG_INFO, "FILE DROPPED !");
            FilePathList droppedFiles = LoadDroppedFiles();
            for (size_t i = 0; i < droppedFiles.count; i++)
            {
                char* filepath = droppedFiles.paths[i];
                Song* song = createSong(song_arena, filepath);
                if (song){
                    core_send_command(core, (CoreCommand) { .type = CMD_QUEUE_ADD, .song = song});
                } else {
                    TraceLog(LOG_ERROR, "Failed to create song at %s.", filepath);
                }
            }
            UnloadDroppedFiles(droppedFiles);
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
                music_slider.value = core->audio->vtable->position(audio) / length;
                if (music_slider.value < 0.f) music_slider.value = 0.f;
                if (music_slider.value > 1.f) music_slider.value = 1.f;
            }
        }

        if (core_get_current_song_playing(core)) {
            Song* song = core_get_current_song_playing(core);
            const char* title = TextFormat("%s - %s - %s", song->title, song->artists, song->album);
            SetWindowTitle(title);
        }

        // START OF THE LAYOUT
        Clay_BeginLayout();
        CLAY(CLAY_ID("WINDOW"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = COLOR_BACKGROUND}) {

            CLAY(CLAY_ID("TABS_CONTAINER"), { .layout = { .layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(80) }, .padding = CLAY_PADDING_ALL(16), .childGap = 16}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                CLAY(CLAY_ID("TABS_QUEUE"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (currentTab == TABS_QUEUE) ? (Clay_Hovered() ? COLOR_ORANGE : COLOR_BLUE) : (Clay_Hovered() ? COLOR_ORANGE : COLOR_BACKGROUND)}) {
                    Clay_OnHover(HandleSelecTabInteraction, (void *)(uintptr_t)TABS_QUEUE);
                    CLAY_TEXT(CLAY_STRING("QUEUE"), CLAY_TEXT_CONFIG({ .fontId = 1, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
                }
                CLAY(CLAY_ID("TABS_SEARCH"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (currentTab == TABS_SEARCH) ? (Clay_Hovered() ? COLOR_ORANGE : COLOR_BLUE) : (Clay_Hovered() ? COLOR_ORANGE : COLOR_BACKGROUND)}) {
                    Clay_OnHover(HandleSelecTabInteraction, (void *)(uintptr_t)TABS_SEARCH);
                    CLAY_TEXT(CLAY_STRING("SEARCH"), CLAY_TEXT_CONFIG({ .fontId = 1, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
                }
                CLAY(CLAY_ID("SEARCH_BAR"), {
                    .layout = {
                        .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER},
                        .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)},
                        .padding = CLAY_PADDING_ALL(8)
                    },
                    .backgroundColor = searchBarActive ? COLOR_BLUE : COLOR_BACKGROUND
                }) {
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
                            .fontSize = 20,
                            .textColor = {150,150,150,255}
                        }));
                    }
                }
            }

            if (currentTab == TABS_QUEUE) {
                CLAY(CLAY_ID("QUEUE_CONTAINER"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }, .backgroundColor = COLOR_BACKGROUND}) {
                    size_t count = core_get_queue_count(core);
                    size_t matchCount = 0;
                    for (size_t i = 0; i < count; i++) {
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
                }
            } else if (currentTab == TABS_SEARCH) {
                CLAY(CLAY_ID("SEARCH_CONTAINER"), {
                    .layout = {
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                        .childGap = 2
                    },
                    .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
                    .backgroundColor = COLOR_BACKGROUND
                }) {
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
                CLAY(CLAY_ID("PLAYER"), { .clip = { .horizontal = true, .vertical = true }, .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .x = CLAY_ALIGN_X_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_PERCENT(0.3)}, .padding = CLAY_PADDING_ALL(8), .childGap = 4 }, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                    Clay_String string_title = { .chars = song->title, .length = strlen(song->title), .isStaticallyAllocated = false };
                    CLAY_TEXT(string_title, CLAY_TEXT_CONFIG({ .fontId = 1, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));

                    Clay_String string_artists = { .chars = song->artists, .length = strlen(song->artists), .isStaticallyAllocated = false };
                    CLAY_TEXT(string_artists, TEXT_CONFIG_24);
                    CLAY(CLAY_ID("SLIDER_FRAME"), { .layout = { .padding = { .left = 8, .right = 8 } , .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .childGap = 8, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {

                        UiTimeString time_played = timeStringFromFloat(ui_arena, core->audio->vtable->position(core->audio));
                        CLAY_TEXT(time_played.clay, CLAY_TEXT_CONFIG({ .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));

                        CLAY(CLAY_ID("SLIDER"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(20)}, .padding = CLAY_PADDING_ALL(2) }, .backgroundColor = COLOR_BACKGROUND}) {
                            Clay_OnHover(HandleSliderInteraction, NULL);
                            float displayValue = (music_slider.value >= 0.f && music_slider.value <= 1.f) ? music_slider.value : 0.f;
                            CLAY(CLAY_ID("SLIDER_WIDGET"), { .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(displayValue), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_BLUE});
                        }

                        UiTimeString time_duration = timeStringFromFloat(ui_arena, song->length);
                        CLAY_TEXT(time_duration.clay, CLAY_TEXT_CONFIG({ .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
                    }

                    CLAY(CLAY_ID("BUTTON_FRAME"), { .layout = { .padding = { 8, 8, 0, 8} , .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = { .x = CLAY_ALIGN_X_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .childGap = 16}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                        CLAY(CLAY_ID("SHUFFLE_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = queue_is_shuffle_enabled(&core->queue) ? (Clay_Hovered() ? COLOR_ORANGE : COLOR_BLUE) : (Clay_Hovered() ? COLOR_ORANGE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandleShuffleInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = &shuffle_tex}, .aspectRatio = { 1 }}) {}
                        }
                        CLAY(CLAY_ID("PREVIOUS_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (Clay_Hovered() ? COLOR_BLUE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandlePreviousInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = &previous_tex}, .aspectRatio = { 1 }}) {}
                        }
                        CLAY(CLAY_ID("PLAY_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (Clay_Hovered() ? COLOR_BLUE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandlePlayInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = (core_get_state(core) == CORE_PLAYING) ? &pause_tex : &play_tex}, .aspectRatio = { 1 }}) {}
                        }
                        CLAY(CLAY_ID("NEXT_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (Clay_Hovered() ? COLOR_BLUE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandleNextInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = &next_tex}, .aspectRatio = { 1 }}) {}
                        }
                        CLAY(CLAY_ID("LOOP_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = core_get_loop_mode(core) ? (Clay_Hovered() ? COLOR_ORANGE : COLOR_BLUE) : (Clay_Hovered() ? COLOR_ORANGE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandleLoopInteraction, NULL);
                            void* img;
                            switch (core_get_loop_mode(core)) {
                                case LOOP_NONE: img = &repeat_tex; break;
                                case LOOP_ONE: img = &repeat_on_one_tex; break;
                                case LOOP_ALL: img = &repeat_on_tex; break;
                                default: img = &repeat_on_one_tex; break;
                            }
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = img }, .aspectRatio = { 1 }}) {}
                        }
                    }
                }
            }
        }

        if (debugEnabled) {
            CLAY(CLAY_ID("DEBUG_PANEL"), { .floating = { .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID, .parentId = CLAY_ID("WINDOW").id }, .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {  .height = CLAY_SIZING_FIT(0), .width = CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(16)}, .backgroundColor = COLOR_ORANGE}) {
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
            }
        }

        Clay_RenderCommandArray renderCommands = Clay_EndLayout();

        BeginDrawing();
        ClearBackground(CLAY_COLOR_TO_RAYLIB_COLOR(COLOR_BACKGROUND));
        Clay_Raylib_Render(renderCommands, fonts);
        if (debugEnabled) DrawFPS(0, 0);
        EndDrawing();

        // RESET UI ARENA FOR THE NEXT PASS
        arena_clear(ui_arena);
    }

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

    UnloadMusicStream(music);
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

        song->id = next_song_id++;

        if (metadata->title_text)
            song->title = arena_strdup(arena, metadata->title_text);
        else song->title = arena_strdup(arena, GetFileNameWithoutExt(filepath));

        if (metadata->artist_text)
            song->artists = arena_strdup(arena, metadata->artist_text);
        else song->artists = arena_strdup(arena, "Unknown Artist");

        if (metadata->album_text)
            song->album = arena_strdup(arena, metadata->album_text);
        else song->album = arena_strdup(arena, "Unknown Album");

        Texture2D tex = texture2DFromImageBuffer(metadata->image);
        song->textureIndex = covers_textures.count;
        vectorAppend(&covers_textures, &tex);

        song->path = arena_strdup(arena, filepath);
        song->length = GetMusicTimeLength(music);
        UnloadMusicStream(music);
        return song;
    }
    return NULL;
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
    printf("%s", errorData.errorText.chars);
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
    CLAY_AUTO_ID({ .layout = { .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(80)}, .childGap = 8 }, .backgroundColor = (is_selected) ? COLOR_BLUE : (is_playing) ? COLOR_ORANGE : (Clay_Hovered() ? COLOR_BACKGROUND : COLOR_BACKGROUND_LIGHT)}) {
        Clay_Color color = (is_selected) ? COLOR_BLUE : (is_playing) ? COLOR_ORANGE : (Clay_Hovered() ? COLOR_BACKGROUND : COLOR_BACKGROUND_LIGHT);
        Clay_OnHover(HandleSongInteraction, song);
        CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(80), .height = CLAY_SIZING_FIXED(80)} }, .image = { .imageData = vectorGet(&covers_textures, song->textureIndex)}, .aspectRatio = { 1 }}) {}
        CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .padding = {8, 8, 8, 8}, .childGap = 16 }, .backgroundColor = color}) {
            Clay_String string_title = { .chars = song->title, .length = strlen(song->title), .isStaticallyAllocated = false };
            CLAY_TEXT(string_title, TEXT_CONFIG_24_BOLD);
            Clay_String string_artists = { .chars = song->artists, .length = strlen(song->artists), .isStaticallyAllocated = false };
            CLAY_TEXT(string_artists, TEXT_CONFIG_24);
        }

        CLAY_AUTO_ID({ .layout = { .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER} , .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .padding = {8, 8, 8, 8}, .childGap = 16 }, .backgroundColor = color}) {
            Clay_String string_album = { .chars = song->album, .length = strlen(song->album), .isStaticallyAllocated = false };
            CLAY_TEXT(string_album, TEXT_CONFIG_24);
        }

        CLAY_AUTO_ID({ .layout = { .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .padding = {8, 8, 8, 8}, .childGap = 16 }, .backgroundColor = color}) {
            UiTimeString time = timeStringFromFloat(ui_arena, song->length);
            CLAY_TEXT(time.clay , TEXT_CONFIG_24);
        }
    }
}

UiTimeString timeStringFromFloat(mem_arena* arena, float seconds)
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

    return (UiTimeString){
        .buf = buf,
        .clay = {
            .chars = buf,
            .length = (h==0) ? 6 : 8,
            .isStaticallyAllocated = false
        }
    };
}

void HandleSliderInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;
    (void)userData;
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
        music_slider.active = false;
    }

    if (!music_slider.active) {
        music_slider.value = core->audio->vtable->position(core->audio) / length;
        if (music_slider.value < 0.f) music_slider.value = 0.f;
        if (music_slider.value > 1.f) music_slider.value = 1.f;
    }
}

void HandleSearchInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;
    (void)userData;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging) {
        searchBarActive = !searchBarActive;
        TraceLog(LOG_INFO, "Search bar active: %d", searchBarActive);
    }
}

void HandleSongInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) elementId;
    Song* song = (Song*)userData;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging) {
        searchBarActive = false;
        Song* curr_selected = core_get_current_song_selected(core);
        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            core_send_command(core, (CoreCommand){.type = CMD_QUEUE_ADD, .song = song});
            return;
        } else if (curr_selected->id == song->id) {
            core_send_command(core, (CoreCommand){ .type = CMD_PLAY_SELECTED });
        } else {
            core_send_command(core, (CoreCommand){ .type = CMD_SELECT_SONG, .song = song});
        }
    }
}

void HandleShuffleInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData){
    (void)userData; (void) elementId;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        searchBarActive = false;
        core_send_command(core, (CoreCommand){ .type = CMD_TOGGLE_SHUFFLE, .shuffle_enabled = !queue_is_shuffle_enabled(&core->queue)});
    }
}

void HandlePreviousInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) elementId; (void)userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        searchBarActive = false;
        core_send_command(core, (CoreCommand) { .type = CMD_PLAY_PREV });
    }
}

void HandleNextInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) elementId; (void)userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME  && !pointer_dragging ) {
        searchBarActive = false;
        core_send_command(core, (CoreCommand) { .type = CMD_PLAY_NEXT });
    }
}

void HandlePlayInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) elementId; (void)userData;
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

    // Convert query to lowercase
    size_t queryLen = strlen(query);
    for (size_t i = 0; i < queryLen && i < MAX_SEARCH_LENGTH - 1; i++) {
        lowerQuery[i] = tolower((unsigned char)query[i]);
    }
    lowerQuery[queryLen] = '\0';

    // Convert title to lowercase
    size_t titleLen = strlen(song->title);
    for (size_t i = 0; i < titleLen && i < 255; i++) {
        lowerTitle[i] = tolower((unsigned char)song->title[i]);
    }
    lowerTitle[titleLen] = '\0';

    // Convert artist to lowercase
    size_t artistLen = strlen(song->artists);
    for (size_t i = 0; i < artistLen && i < 255; i++) {
        lowerArtist[i] = tolower((unsigned char)song->artists[i]);
    }
    lowerArtist[artistLen] = '\0';

    // Convert album to lowercase
    size_t albumLen = strlen(song->album);
    for (size_t i = 0; i < albumLen && i < 255; i++) {
        lowerAlbum[i] = tolower((unsigned char)song->album[i]);
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
