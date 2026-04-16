#include "song.h"


Song* createSong(char* filepath, Vector* pending_textures, mem_arena* song_arena, int* next_song_id, mem_arena* string_arena, Vector* covers_textures, pthread_mutex_t* arena_mutex)
{
    if (!FileExists(filepath))
        return NULL;

    mem_arena* local_scratch = arena_create(MiB(8), MiB(1));
    Metadata* metadata = get_metadata_from_mp3(local_scratch, filepath);

    Music music = LoadMusicStream(filepath);
    float song_length = GetMusicTimeLength(music);
    UnloadMusicStream(music);

    pthread_mutex_lock(arena_mutex);
    Song* song = PUSH_STRUCT(song_arena, Song);
    memset(song, 0, sizeof(Song));

    song->id = (*next_song_id)++;

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

    song->textureIndex = covers_textures->count;
    Texture2D placeholder = {0};
    vectorAppend(covers_textures, &placeholder);
    pthread_mutex_unlock(arena_mutex);


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

            pthread_mutex_lock(arena_mutex);
            vectorAppend(pending_textures, &pending);
            pthread_mutex_unlock(arena_mutex);
        }
    }

    arena_destroy(local_scratch);
    return song;
}
