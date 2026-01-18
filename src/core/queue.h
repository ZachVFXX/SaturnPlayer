// core/queue.h
#pragma once
#include <stddef.h>
#include "song.h"
#include "stdbool.h"

typedef enum {
    LOOP_NONE = 0,    // No looping
    LOOP_ALL,         // Loop entire queue
    LOOP_ONE          // Loop current song
} LoopMode;

typedef struct {
    Song **items;
    size_t count;
    size_t capacity;
    size_t current_selected;
    size_t current_playing;

    bool shuffle_enabled;
    LoopMode loop_mode;
    size_t *shuffle_indices;  // Array of shuffled indices
} SongQueue;

void queue_init(SongQueue* q);
void queue_free(SongQueue* q);
void queue_push(SongQueue* q, Song* song);
void queue_remove(SongQueue* q, Song* song);

Song* queue_current_playing(SongQueue* q);
Song* queue_current_selected(SongQueue* q);
Song* queue_get_at(SongQueue* q, size_t index);

Song* queue_play_next(SongQueue* q);
Song* queue_play_prev(SongQueue* q);
Song* queue_play(SongQueue* q, size_t index);
Song* queue_play_song(SongQueue* q, Song* song);

Song* queue_select_next(SongQueue* q);
Song* queue_select_prev(SongQueue* q);
Song* queue_select(SongQueue* q, size_t index);
Song* queue_select_song(SongQueue* q, Song* song);

void queue_set_shuffle(SongQueue* q, bool enabled);
void queue_set_loop_mode(SongQueue* q, LoopMode mode);
bool queue_is_shuffle_enabled(SongQueue* q);
LoopMode queue_get_loop_mode(SongQueue* q);
