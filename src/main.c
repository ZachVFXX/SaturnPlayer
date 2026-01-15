#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
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

#define COLOR_BACKGROUND (Clay_Color) {200, 200, 200, 20}
#define COLOR_BACKGROUND_LIGHT (Clay_Color) {200, 200, 200, 40}

typedef Vector Playlist;

typedef struct {
    char* path;
    char* title;
    char* artists;
    char* album;
    ImageBuffer* img;
} Song;

typedef struct {
    Playlist* songs;
    int current_index;
} Queue;

typedef struct {
    Queue* queue;
} Context;


typedef struct
{
    Clay_Vector2 clickOrigin;
    Clay_Vector2 positionOrigin;
    bool mouseDown;
} ScrollbarData;

void selectIndex(Queue *queue, int index);
void playIndex(Queue queue, int index);
Song* createSong(mem_arena* arena, char* filepath);
bool addSongToQueue(Queue *queue, Song *song, int index);
bool removeSongFromQueue(Queue *queue, int index);
Texture2D texture_from_image_buffer(ImageBuffer* img);

// CLAY RENDER
void HandleClayErrors(Clay_ErrorData errorData);
bool reinitializeClay = false;
bool debugEnabled = false;
ScrollbarData scrollbarData = {0};

Queue queue = {0};
Music music = {0};
mem_arena* song_arena = {0};

void HandleButtonInteraction(Clay_ElementId elementId, Clay_PointerData pointerInfo, void *userData) {
    int index = *(int*)userData;
    if (pointerInfo.state == CLAY_POINTER_DATA_RELEASED_THIS_FRAME || Clay_PointerOver(elementId)) {
        selectIndex(&queue, index);
        playIndex(queue, index);
    }
}

void RenderSong(Song song, int index) {
    int* indexes = arena_push(song_arena, sizeof(index), true);
    memcpy(indexes, &index, sizeof(index));

    Clay_String clay_string = { .chars = song.title, .length = strlen(song.title), .isStaticallyAllocated = false };
    CLAY_AUTO_ID({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(40)}, .padding = {8, 8, 4, 4} }, .backgroundColor = (index == queue.current_index) ? COLOR_BLUE : (Clay_Hovered() ? COLOR_BACKGROUND : COLOR_BACKGROUND_LIGHT) }) {
        Clay_OnHover(HandleButtonInteraction, indexes);
        CLAY_TEXT(clay_string, CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = {255,255,255,255} }));
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
    Vector covers_textures = {0};

    song_arena = arena_create(GiB(1), MiB(1));

    vectorInit(&songs, sizeof(Song), 32);
    vectorInit(&covers_textures, sizeof(Song), 32);

    queue.songs=&songs;
    queue.current_index=0;

    FilePathList music_files = LoadDirectoryFilesEx("/home/zach/.pymusicterm/musics/", ".mp3", false);

    for (int i = 0; i < music_files.count; i++)
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
        if (IsKeyPressed(KEY_S)) {selectIndex(&queue, queue.current_index + 1); playIndex(queue, queue.current_index);}
        if (IsKeyPressed(KEY_W)) {selectIndex(&queue, queue.current_index - 1); playIndex(queue, queue.current_index);}
        if (IsKeyPressed(KEY_R)) {removeSongFromQueue(&queue, queue.current_index);}
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

        if (!IsMouseButtonDown(0)) {
            scrollbarData.mouseDown = false;
        }

        // CHECK IF SCROLLBAR IS USED
        if (IsMouseButtonDown(0) && !scrollbarData.mouseDown && Clay_PointerOver(Clay_GetElementId(CLAY_STRING("ScrollBar")))) {
            Clay_ScrollContainerData scrollContainerData = Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("MainContent")));
            scrollbarData.clickOrigin = mousePosition;
            scrollbarData.positionOrigin = *scrollContainerData.scrollPosition;
            scrollbarData.mouseDown = true;
        } else if (scrollbarData.mouseDown) {
            Clay_ScrollContainerData scrollContainerData = Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("MainContent")));
            if (scrollContainerData.contentDimensions.height > 0) {
                Clay_Vector2 ratio = (Clay_Vector2) {
                    scrollContainerData.contentDimensions.width / scrollContainerData.scrollContainerDimensions.width,
                    scrollContainerData.contentDimensions.height / scrollContainerData.scrollContainerDimensions.height,
                };
                if (scrollContainerData.config.vertical) {
                    scrollContainerData.scrollPosition->y = scrollbarData.positionOrigin.y + (scrollbarData.clickOrigin.y - mousePosition.y) * ratio.y;
                }
                if (scrollContainerData.config.horizontal) {
                    scrollContainerData.scrollPosition->x = scrollbarData.positionOrigin.x + (scrollbarData.clickOrigin.x - mousePosition.x) * ratio.x;
                }
            }
        }

        // UPDATE THE SCROLL ACCORDINGLY
        Clay_UpdateScrollContainers(true, (Clay_Vector2) {mouseWheelX, mouseWheelY}, GetFrameTime());

        // START OF THE LAYOUT
        Clay_BeginLayout();

        CLAY(CLAY_ID("WINDOW"), { .layout = { .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = COLOR_BACKGROUND}) {
            CLAY(CLAY_ID("PLAYLIST_CONTAINER"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { .height = CLAY_SIZING_GROW(0) }, .childGap = 2 }, .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }, .backgroundColor = COLOR_BACKGROUND_LIGHT}) {
                for (int i = 0; i < queue.songs->count; i++) {
                    Song song = *(Song*)vectorGet(&songs, i);
                    RenderSong(song, i);
                }
            }
        }



        // All clay layouts are declared between Clay_BeginLayout and Clay_EndLayout
        Clay_RenderCommandArray renderCommands = Clay_EndLayout();

        // RAYLIB drawing of the render commands
        BeginDrawing();
        ClearBackground(BLACK);
        if (IsMusicValid(music)) UpdateMusicStream(music);
        Clay_Raylib_Render(renderCommands, fonts);
        EndDrawing();
    }

    vectorFree(&songs);
    vectorFree(&covers_textures);
    arena_destroy(song_arena);
    UnloadDirectoryFiles(music_files);
    CloseAudioDevice();
    Clay_Raylib_Close();
    return 0;
}

Song* createSong(mem_arena* arena, char* filepath)
{
    if (FileExists(filepath))
    {

        Metadata* metadata = get_metadata_from_mp3(arena, filepath);
        Song* song = PUSH_STRUCT(arena, Song);
        song->title = metadata->title_text;
        song->artists = metadata->artist_text;
        song->album = metadata->album_text;
        song->img = metadata->image;
        song->path = filepath;
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
    return true;
}

void selectIndex(Queue *queue, int index)
{
    if (index < 0) index = queue->songs->count - 1;
    if ((unsigned) index >= queue->songs->count) index = 0;
    queue->current_index = index;
    TraceLog(LOG_INFO, "Selecting index at index %d.", index);

}

void playIndex(Queue queue, int index)
{
    if (IsMusicValid(music)) UnloadMusicStream(music);
    music = LoadMusicStream(((Song*)vectorGet(queue.songs, index))->path);
    PlayMusicStream(music);
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
