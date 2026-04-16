#ifndef SONG_H
#define SONG_H

#include <stdint.h>
#include "../../external/raylib/src/raylib.h"
#include <pthread.h>
#include "../metadata/metadata.h"
#include "../utils/arena.h"
#include "../utils/vector.h"

typedef uint32_t str_id;

typedef struct {
    Image image;
    int textureIndex;
} PendingTexture;

typedef struct {
    str_id path;
    str_id title;
    str_id artists;
    str_id album;
    float length;
    uint16_t textureIndex;
    uint16_t id;
} Song;

Song* createSong(char* filepath, Vector* pending_textures, mem_arena* song_arena, int* next_song_id, mem_arena* string_arena, Vector* covers_textures, pthread_mutex_t* arena_mutex);

#endif
