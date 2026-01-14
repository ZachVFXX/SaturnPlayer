#include <raylib.h>
#include "metadata.c"

#define RAYGUI_IMPLEMENTATION
#include "../external/raygui.h"
#define VECTOR_IMPLEMENTATION
#include "vector.h"

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define PADDING 10

Music music = {0};

typedef struct {
    char* path;
    char* title;
    char* artists;
    char* album;
    ImageBuffer* img;
} Song;

typedef struct {
    Vector* songs;
    int current_index;
} Queue;

void selectIndex(Queue *queue, int index);
void playIndex(Queue queue, int index);
bool addSongToQueue(Queue *queue, char *path, int index);
bool removeSongFromQueue(Queue *queue, int index);

int main(void)
{
    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "Music Player");
    InitAudioDevice();
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    Vector songs = {0};
    vectorInit(&songs, sizeof(Song), 32);

    Queue queue = {
        .songs=&songs,
        .current_index=0,
    };

    FilePathList music_files = LoadDirectoryFilesEx("/home/zach/.pymusicterm/musics/", ".mp3", false);

    float value = 0;

    Rectangle queuePanel = { PADDING, PADDING, 300 + PADDING, DEFAULT_HEIGHT/2.f};
    Rectangle panelContentRec = {PADDING, PADDING, 290, DEFAULT_HEIGHT/2.f    };
    Rectangle panelView = { 0 };
    Vector2 panelScroll = { 1, 1 };

    for (int i = 0; i < music_files.count; i++)
    {
        addSongToQueue(&queue, music_files.paths[i], queue.songs->count);
    }

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_S)) selectIndex(&queue, queue.current_index + 1);
        if (IsKeyPressed(KEY_W)) selectIndex(&queue, queue.current_index - 1);
        if (IsKeyPressed(KEY_ENTER)) playIndex(queue, queue.current_index);
        if (IsKeyPressed(KEY_Q)) addSongToQueue(&queue, "/home/zach/Dev/CMusicPlayer/T8pmUf7QKQg.mp3", queue.current_index + 1);
        if (IsKeyPressed(KEY_R)) removeSongFromQueue(&queue, queue.current_index);
        if (IsKeyPressed(KEY_SPACE) && IsMusicValid(music)) {
            if (IsMusicStreamPlaying(music)) {
                PauseMusicStream(music);
            } else {
                ResumeMusicStream(music);
            }
        }

        BeginDrawing();
            ClearBackground(WHITE);

            GuiScrollPanel(queuePanel, NULL, panelContentRec, &panelScroll, &panelView);
            BeginScissorMode(panelView.x, panelView.y, panelView.width, panelView.height);
            for (int i = 0; i < queue.songs->count; i++) {
                Song song = *((Song*)vectorGet(queue.songs, i));
                const char* title = TextFormat("%s - %s - %s", song.title, song.album, song.artists);
                if (GuiButton((Rectangle){queuePanel.x + panelScroll.x, (queuePanel.y + panelScroll.y) + 30 * i, 300, 30}, title)) {
                    selectIndex(&queue, i);
                    playIndex(queue, i);
                }
            }
            EndScissorMode();

            if (IsMusicValid(music)) {
                UpdateMusicStream(music);
                if (GetMusicTimeLength(music) < GetMusicTimePlayed(music) + 0.1) {
                    selectIndex(&queue, queue.current_index + 1);
                    playIndex(queue, queue.current_index);
                }
            }

            float current_time = GetMusicTimePlayed(music);
            float duration = GetMusicTimeLength(music);
            if (GuiSliderBar((Rectangle){ 40, GetScreenHeight() * 0.85, GetScreenWidth() - 80,  15 }, TextFormat("%.1f", current_time), TextFormat("%.1f", duration), &value, 0, GetMusicTimeLength(music))){
                if(IsMusicValid(music)) SeekMusicStream(music, value);
            }
            value = current_time;
            if (GuiButton((Rectangle){ GetScreenWidth() * 0.5 - 20, GetScreenHeight() * 0.9, 40, 40 }, IsMusicStreamPlaying(music) ? "Pause" : "Play") && IsMusicValid(music)) {
                if (IsMusicStreamPlaying(music)) {
                    PauseMusicStream(music);
                } else {
                    ResumeMusicStream(music);
                }
            }

            if (GuiButton((Rectangle){ GetScreenWidth() * 0.4 - 20, GetScreenHeight() * 0.9, 40, 40 }, "Previous") && IsMusicValid(music)) {
                selectIndex(&queue, queue.current_index - 1);
                playIndex(queue, queue.current_index);
            }

            if (GuiButton((Rectangle){ GetScreenWidth() * 0.6 - 20, GetScreenHeight() * 0.9, 40, 40 }, "Next") && IsMusicValid(music)) {
                selectIndex(&queue, queue.current_index + 1);
                playIndex(queue, queue.current_index);
            }

        EndDrawing();
    }

    vectorFree(&songs);
    UnloadDirectoryFiles(music_files);
    CloseWindow();
    CloseAudioDevice();
    return 0;
}

bool addSongToQueue(Queue *queue, char *filepath, int index)
{
    Metadata* metadata = get_metadata_from_mp3(filepath);
    Song song = { // TODO: OPTIMIZE BY NOT REFERING TO METADATA
        .title = metadata->title_text,
        .artists = metadata->artist_text,
        .album = metadata->album_text,
        .img = metadata->image,
        .path = filepath,
    };
    if(vectorInsert(queue->songs, index, &song) < 0) {
        TraceLog(LOG_ERROR, "Inserting the song in the queue failed.");
        return false;
    }
    TraceLog(LOG_INFO, "Title: %s, Artists: %s, Album: %s, Path: %s", song.title, song.artists, song.album, song.path);
    return true;
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
