#include "raylib.h"

#define CLAY_IMPLEMENTATION
#include "../external/clay.h"
#include "../external/raylib_renderer.c"

#define VECTOR_IMPLEMENTATION
#include "utils/vector.h"
#define ARENA_IMPLEMENTATION
#include "utils/arena.h"

#include "metadata.c"

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600

#define COLOR_ORANGE (Clay_Color) {225, 138, 50, 255}
#define COLOR_BLUE (Clay_Color) {111, 173, 162, 255}

#define IMGS_PATH "../assets/imgs/"
#define FONTS_PATH "../assets/fonts/"

#include "assets/Poppins_Regular.h"
#include "assets/Poppins_SemiBold.h"
#include "assets/loop.h"
#include "assets/next.h"
#include "assets/pause.h"
#include "assets/play.h"
#include "assets/previous.h"
#include "assets/shuffle.h"


#define COLOR_BACKGROUND (Clay_Color) {20, 20, 20, 255}
#define COLOR_BACKGROUND_LIGHT (Clay_Color) {40, 40, 40, 255}
#define TEXT_CONFIG_24 CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} })
#define TEXT_CONFIG_24_BOLD CLAY_TEXT_CONFIG({ .fontId = 1, .fontSize = 24, .textColor = {255,255,255,255} })

#define RAYLIB_VECTOR2_TO_CLAY_VECTOR2(vector) (Clay_Vector2) { .x = vector.x, .y = vector.y }
#define CLAY_COLOR_TO_RAYLIB_COLOR(color) (Color) { .r = (unsigned char)roundf(color.r), .g = (unsigned char)roundf(color.g), .b = (unsigned char)roundf(color.b), .a = (unsigned char)roundf(color.a) }

typedef Vector Playlist;

typedef struct {
    char* path;
    char* title;
    char* artists;
    char* album;
    float_t length; // second
    int textureIndex;
    int id;
} Song;

typedef struct {
    Playlist* songs;
    int32_t current_id_selected;
    int32_t current_id_playing;
} Queue;

typedef struct {
    bool active;        // currently dragging
    float value;        // 0..1 normalized
} SliderState;

typedef struct {
    char* buf;
    Clay_String clay;
} UiTimeString;

void selectId(Queue* queue, int id);
void playPrevious(Queue* queue);
void playNext(Queue* queue);
void selectPrevious(Queue* queue);
void selectNext(Queue* queue);
void playId(Queue* queue, int id);
Song* createSong(mem_arena* arena, char* filepath);
bool addSongToQueue(Queue* queue, Song* song, int index);
bool removeSongFromQueue(Queue *queue, int id);
int findSongById(Queue* queue, int id);
Texture2D texture2DFromImageBuffer(ImageBuffer* img);
void renderSong(Song song);
UiTimeString timeStringFromFloat(mem_arena* arena, float seconds);
Texture2D createTextureFromMemory(unsigned char* data, int format, int width, int height);

// CLAY RENDER
void HandleClayErrors(Clay_ErrorData errorData);
bool reinitializeClay = false;
bool clayDebugEnabled = false;

void HandleSongInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleSliderInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandlePreviousInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandlePlayInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleNextInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleShuffleInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);
void HandleLoopInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData);

// Player
Music music = {0};
Vector covers_textures = {0};
mem_arena* song_arena = {0};
mem_arena* ui_arena = {0};
Playlist songs = {0};
Queue queue = {0};
SliderState music_slider = {0};
bool debugEnabled = false;
bool isLooping = false;

static int next_song_id = 0;

int main(void) {
    // Clay initialisation
    int totalMemorySize = Clay_MinMemorySize();
    Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));
    Clay_Initialize(clayMemory, (Clay_Dimensions) { (float)DEFAULT_WIDTH, (float)DEFAULT_HEIGHT }, (Clay_ErrorHandler) { HandleClayErrors, 0 });

    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "Music Player");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetWindowState(FLAG_MSAA_4X_HINT);
    InitAudioDevice();
    SetTargetFPS(240);

    Texture2D loop_tex = createTextureFromMemory(LOOP_DATA, LOOP_FORMAT, LOOP_WIDTH, LOOP_HEIGHT);
    Texture2D next_tex = createTextureFromMemory(NEXT_DATA, NEXT_FORMAT, NEXT_WIDTH, NEXT_HEIGHT);
    Texture2D previous_tex = createTextureFromMemory(PREVIOUS_DATA, PREVIOUS_FORMAT, PREVIOUS_WIDTH, PREVIOUS_HEIGHT);
    Texture2D play_tex = createTextureFromMemory(PLAY_DATA, PLAY_FORMAT, PLAY_WIDTH, PLAY_HEIGHT);
    Texture2D pause_tex = createTextureFromMemory(PAUSE_DATA, PAUSE_FORMAT, PAUSE_WIDTH, PAUSE_HEIGHT);
    Texture2D shuffle_tex = createTextureFromMemory(SHUFFLE_DATA, SHUFFLE_FORMAT, SHUFFLE_WIDTH, SHUFFLE_HEIGHT);

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

    vectorInit(&songs, sizeof(Song), 32);
    vectorInit(&covers_textures, sizeof(Texture2D), 32);

    queue.songs = &songs;
    queue.current_id_selected = 0;

    FilePathList music_files = LoadDirectoryFilesEx("/home/zach/.pymusicterm/musics/", ".mp3", false);

    for (size_t i = 0; i < music_files.count; i++)
    {
        char* filepath = music_files.paths[i];
        Song* song = createSong(song_arena, filepath);
        if (song){
            addSongToQueue(&queue, song, queue.songs->count);
        } else {
            TraceLog(LOG_ERROR, "Failed to create song at %s.", filepath);
        }
    }

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_S)) selectNext(&queue);
        if (IsKeyPressed(KEY_W)) selectPrevious(&queue);
        if (IsKeyPressed(KEY_R)) removeSongFromQueue(&queue, queue.current_id_selected);
        if (IsKeyPressed(KEY_ENTER)) playId(&queue, queue.current_id_selected);
        if (IsKeyPressed(KEY_D) && IsMusicValid(music)) SeekMusicStream(music, GetMusicTimePlayed(music) + 5);
        if (IsKeyPressed(KEY_A) && IsMusicValid(music)) SeekMusicStream(music, GetMusicTimePlayed(music) - 5);
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

        // CLAY DEBUG MODE
        if (IsKeyPressed(KEY_H)) {
            clayDebugEnabled = !clayDebugEnabled;
            Clay_SetDebugModeEnabled(clayDebugEnabled);
        }

        if (IsKeyPressed(KEY_F3)) {
            debugEnabled = !debugEnabled;

        }

        if (IsFileDropped()) {
            TraceLog(LOG_INFO, "FILE DROPPED !");
            FilePathList droppedFiles = LoadDroppedFiles();
            for (size_t i = 0; i < droppedFiles.count; i++)
            {
                char* filepath = droppedFiles.paths[i];
                Song* song = createSong(song_arena, filepath);
                if (song){
                    addSongToQueue(&queue, song, queue.songs->count);
                } else {
                    TraceLog(LOG_ERROR, "Failed to create song at %s.", filepath);
                }
            }
            UnloadDroppedFiles(droppedFiles);
        }

        //GET DEFAULT DATA FOR MOUSE AND DIMENSION
        Vector2 mouseWheelDelta = GetMouseWheelMoveV();
        float mouseWheelX = mouseWheelDelta.x;
        float mouseWheelY = mouseWheelDelta.y;
        Clay_Vector2 mousePosition = RAYLIB_VECTOR2_TO_CLAY_VECTOR2(GetMousePosition());
        Clay_SetPointerState(mousePosition, IsMouseButtonDown(0));
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

        if (IsMusicStreamPlaying(music)) {
            if (GetMusicTimePlayed(music) + 0.1 >= GetMusicTimeLength(music)) {
                if (isLooping) continue;
                selectNext(&queue);
                playId(&queue, queue.current_id_selected);
            }
        }

        // START OF THE LAYOUT
        Clay_BeginLayout();
        CLAY(CLAY_ID("WINDOW"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = COLOR_BACKGROUND}) {
            CLAY(CLAY_ID("PLAYLIST_CONTAINER"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }}) {
                for (size_t i = 0; i < queue.songs->count; i++) {
                    Song song = *(Song*)vectorGet(&songs, i);
                    renderSong(song);
                }
            }
            if (IsMusicValid(music)) {
                int current_idx = findSongById(&queue, queue.current_id_playing);
                Song song = *(Song*)vectorGet(&songs, current_idx);
                CLAY(CLAY_ID("PLAYER"), { .clip = { .horizontal = true, .vertical = true }, .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .x = CLAY_ALIGN_X_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_PERCENT(0.3)}, .padding = CLAY_PADDING_ALL(8), .childGap = 4 }, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                    Clay_String string_title = { .chars = song.title, .length = strlen(song.title), .isStaticallyAllocated = false };
                    CLAY_TEXT(string_title, CLAY_TEXT_CONFIG({ .fontId = 1, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));

                    Clay_String string_artists = { .chars = song.artists, .length = strlen(song.artists), .isStaticallyAllocated = false };
                    CLAY_TEXT(string_artists, TEXT_CONFIG_24);
                    CLAY(CLAY_ID("SLIDER_FRAME"), { .layout = { .padding = { .left = 8, .right = 8 } , .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .childGap = 8, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {

                        UiTimeString time_played = timeStringFromFloat(ui_arena, GetMusicTimePlayed(music));
                        CLAY_TEXT(time_played.clay, CLAY_TEXT_CONFIG({ .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));

                        CLAY(CLAY_ID("SLIDER"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(20)}, .padding = CLAY_PADDING_ALL(2) }, .backgroundColor = COLOR_BACKGROUND}) {
                            Clay_OnHover(HandleSliderInteraction, NULL);
                            float displayValue = (IsMusicValid(music) && music_slider.value >= 0.f && music_slider.value <= 1.f) ? music_slider.value : 0.f;
                            CLAY(CLAY_ID("SLIDER_WIDGET"), { .layout = { .sizing = { .width = CLAY_SIZING_PERCENT(displayValue), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_BLUE});
                        }

                        UiTimeString time_duration = timeStringFromFloat(ui_arena, song.length);
                        CLAY_TEXT(time_duration.clay, CLAY_TEXT_CONFIG({ .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
                    }

                    CLAY(CLAY_ID("BUTTON_FRAME"), { .layout = { .padding = { 8, 8, 0, 8} , .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = { .x = CLAY_ALIGN_X_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .childGap = 16}, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                        CLAY(CLAY_ID("SHUFFLE_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (Clay_Hovered() ? COLOR_ORANGE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandleShuffleInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = &shuffle_tex}, .aspectRatio = { 1 }}) {}
                            //CLAY_TEXT(CLAY_STRING("󰒝"), CLAY_TEXT_CONFIG({ .fontId = 2, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
                        }
                        CLAY(CLAY_ID("PREVIOUS_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (Clay_Hovered() ? COLOR_BLUE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandlePreviousInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = &previous_tex}, .aspectRatio = { 1 }}) {}
                            //CLAY_TEXT(CLAY_STRING("󰒮"), CLAY_TEXT_CONFIG({ .fontId = 2, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
                        }
                        CLAY(CLAY_ID("PLAY_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (Clay_Hovered() ? COLOR_BLUE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandlePlayInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = (IsMusicStreamPlaying(music)) ? &pause_tex : &play_tex}, .aspectRatio = { 1 }}) {}
                            //CLAY_TEXT((IsMusicStreamPlaying(music) == 0) ? CLAY_STRING("󰐊") : CLAY_STRING("󰏤") , CLAY_TEXT_CONFIG({ .fontId = 2, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
                        }
                        CLAY(CLAY_ID("NEXT_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = (Clay_Hovered() ? COLOR_BLUE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandleNextInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = &next_tex}, .aspectRatio = { 1 }}) {}
                            //CLAY_TEXT(CLAY_STRING("󰒭"), CLAY_TEXT_CONFIG({ .fontId = 2, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
                        }
                        CLAY(CLAY_ID("LOOP_BUTTON"), { .layout = { .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}, .sizing = { .height = CLAY_SIZING_GROW(0), .width = CLAY_SIZING_GROW(0)}}, .backgroundColor = isLooping ? (Clay_Hovered() ? COLOR_ORANGE : COLOR_BLUE) : (Clay_Hovered() ? COLOR_ORANGE : COLOR_BACKGROUND)}) {
                            Clay_OnHover(HandleLoopInteraction, NULL);
                            CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24)} }, .image = { .imageData = &loop_tex }, .aspectRatio = { 1 }}) {}
                            //CLAY_TEXT(CLAY_STRING("󰛤"), CLAY_TEXT_CONFIG({ .fontId = 2, .textAlignment = CLAY_TEXT_ALIGN_CENTER, .fontSize = 24, .textColor = {255,255,255,255} }));
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

        // All clay layouts are declared between Clay_BeginLayout and Clay_EndLayout
        Clay_RenderCommandArray renderCommands = Clay_EndLayout();

        // RAYLIB drawing of the render commands
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

    UnloadTexture(loop_tex);
    UnloadTexture(play_tex);
    UnloadTexture(next_tex);
    UnloadTexture(previous_tex);
    UnloadTexture(pause_tex);
    UnloadTexture(shuffle_tex);

    UnloadMusicStream(music);
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


bool addSongToQueue(Queue *queue, Song *song, int index)
{
    if(vectorInsert(queue->songs, index, song) < 0) {
        TraceLog(LOG_ERROR, "Inserting the song in the queue failed.");
        return false;
    }
    TraceLog(LOG_INFO, "Title: %s, Artists: %s, Album: %s, Path: %s", song->title, song->artists, song->album, song->path);
    return true;
}

Texture2D texture2DFromImageBuffer(ImageBuffer* img)
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


bool removeSongFromQueue(Queue *queue, int id)
{
    int index = findSongById(queue, id);
    if (index < 0) {
        TraceLog(LOG_ERROR, "Id %d does not exist in queue.", id);
        return false;
    }
    int new_index = 0;
    if (queue->current_id_playing == id) {
        if ((size_t) index > queue->songs->count - 1) {
            new_index = 0;
        } else {
            new_index = index + 1;
        }

        Song* new_song = (Song*)vectorGet(queue->songs, new_index);
        playId(queue, new_song->id);
    }

    if (queue->current_id_selected == id) {
        if ((size_t) index > queue->songs->count - 1) {
            new_index = 0;
        } else {
            new_index = index + 1;
        }

        Song* new_song = (Song*)vectorGet(queue->songs, new_index);
        selectId(queue, new_song->id);
    }


    if(vectorRemove(queue->songs, index) < 0) {
        TraceLog(LOG_ERROR, "Removing the song in the queue failed at index %d.", index);
        return false;
    }
    return true;
}

void playPrevious(Queue* queue)
{
    selectPrevious(queue);
    playId(queue, queue->current_id_selected);
}

void playNext(Queue* queue)
{
    selectNext(queue);
    playId(queue, queue->current_id_selected);
}

void selectNext(Queue* queue)
{
    int current_index = findSongById(queue, queue->current_id_selected);
    int new_index = current_index + 1;
    if ((size_t) new_index > queue->songs->count - 1) {
        new_index = 0;
    }
    selectId(queue, ((Song*)vectorGet(queue->songs, new_index))->id);
}

void selectPrevious(Queue* queue)
{
    int current_index = findSongById(queue, queue->current_id_selected);
    int new_index = current_index - 1;
    if (new_index < 0) {
        new_index = queue->songs->count - 1;
    }
    selectId(queue, ((Song*)vectorGet(queue->songs, new_index))->id);
}

void selectId(Queue* queue, int id)
{
    int index = findSongById(queue, id);
    if (index < 0) {
        TraceLog(LOG_ERROR, "Id %d does not exist.", id);
    }
    if ((size_t) index >= queue->songs->count) index = 0;
    queue->current_id_selected = ((Song*) vectorGet(queue->songs, index))->id;
    TraceLog(LOG_INFO, "Selecting id %d.", id);

}

int findSongById(Queue* queue, int id) {
    for (size_t i = 0; i < queue->songs->count; i++) {
        Song* song = (Song*)vectorGet(queue->songs, i);
        if (song->id == id) return i;
    }
    return -1;
}

void playId(Queue* queue, int id)
{
    if (IsMusicValid(music)) UnloadMusicStream(music);
    if ((size_t) id < queue->songs->count || id > 0) {
        int index = findSongById(queue, id);
        if (index < 0) return;
        music = LoadMusicStream(((Song*)vectorGet(queue->songs, index))->path);
        PlayMusicStream(music);
        queue->current_id_playing = id;
    }
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


void renderSong(Song song) {
    int id = song.id;
    CLAY_AUTO_ID({ .layout = { .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(80)}, .childGap = 8 }, .backgroundColor =  (id == queue.current_id_selected) ? COLOR_BLUE : (id == queue.current_id_playing) ? COLOR_ORANGE : (Clay_Hovered() ? COLOR_BACKGROUND : COLOR_BACKGROUND_LIGHT)}) {
        Clay_Color color = (id == queue.current_id_selected) ? COLOR_BLUE : (id == queue.current_id_playing) ? COLOR_ORANGE : (Clay_Hovered() ? COLOR_BACKGROUND : COLOR_BACKGROUND_LIGHT);
        Clay_OnHover(HandleSongInteraction, (void*)(intptr_t)id);
        CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_FIXED(80), .height = CLAY_SIZING_FIXED(80)} }, .image = { .imageData = vectorGet(&covers_textures, song.textureIndex)}, .aspectRatio = { 1 }}) {}
        CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .padding = {8, 8, 8, 8}, .childGap = 16 }, .backgroundColor = color}) {
            Clay_String string_title = { .chars = song.title, .length = strlen(song.title), .isStaticallyAllocated = false };
            CLAY_TEXT(string_title, TEXT_CONFIG_24_BOLD);
            Clay_String string_artists = { .chars = song.artists, .length = strlen(song.artists), .isStaticallyAllocated = false };
            CLAY_TEXT(string_artists, TEXT_CONFIG_24);
        }

        CLAY_AUTO_ID({ .layout = { .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER} , .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .padding = {8, 8, 8, 8}, .childGap = 16 }, .backgroundColor = color}) {
            Clay_String string_album = { .chars = song.album, .length = strlen(song.album), .isStaticallyAllocated = false };
            CLAY_TEXT(string_album, TEXT_CONFIG_24);
        }

        CLAY_AUTO_ID({ .layout = { .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER }, .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}, .padding = {8, 8, 8, 8}, .childGap = 16 }, .backgroundColor = color}) {
            UiTimeString time = timeStringFromFloat(ui_arena, song.length);
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

void HandlePreviousInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME || Clay_PointerOver(elementId)) {
        playPrevious(&queue);
    }
}

void HandlePlayInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME || Clay_PointerOver(elementId)) {
        if (IsMusicStreamPlaying(music)) {
                PauseMusicStream(music);
            } else {
                ResumeMusicStream(music);
        }
    }
}

void HandleNextInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void) userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME || Clay_PointerOver(elementId)) {
        playNext(&queue);
    }
}

void HandleSongInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    int id = (int)(intptr_t)userData;

    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME || Clay_PointerOver(elementId)) {
        if (id == queue.current_id_selected) {
            playId(&queue, queue.current_id_selected);
        } else {
            selectId(&queue, id);
        }
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

void HandleShuffleInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;
    (void)userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME || Clay_PointerOver(elementId)) {
        vectorShuffle(queue.songs);
    }
}

void HandleLoopInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    (void)elementId;
    (void)userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME || Clay_PointerOver(elementId)) {
        isLooping = !isLooping;
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
