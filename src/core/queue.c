#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

void queue_init(SongQueue *q)
{
    q->items = NULL;
    q->count = 0;
    q->capacity = 0;
    q->current_selected = 0;
    q->current_playing = 0;
    q->shuffle_enabled = false;
    q->loop_mode = LOOP_NONE;
    q->shuffle_indices = NULL;
}

void queue_free(SongQueue *q)
{
    free(q->items);
    free(q->shuffle_indices);
    q->items = NULL;
    q->shuffle_indices = NULL;
    q->count = 0;
    q->capacity = 0;
}
static void queue_generate_shuffle(SongQueue *q)
{
    if (q->count == 0) return;

    if (!q->shuffle_indices) {
        q->shuffle_indices = malloc(q->capacity * sizeof(size_t));
    }

    for (size_t i = 0; i < q->count; i++) {
        q->shuffle_indices[i] = i;
    }

    // Fisher-Yates shuffle
    srand(time(NULL));
    for (size_t i = q->count - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        size_t temp = q->shuffle_indices[i];
        q->shuffle_indices[i] = q->shuffle_indices[j];
        q->shuffle_indices[j] = temp;
    }
}

Song* queue_get_at(SongQueue *q, size_t index)
{
    if (q->count == 0 || index >= q->count) return NULL;

    if (q->shuffle_enabled && q->shuffle_indices) {
        size_t actual_idx = q->shuffle_indices[index];
        return q->items[actual_idx];
    }

    return q->items[index];
}

void queue_push(SongQueue *q, Song *song)
{
    if (q->count == q->capacity) {
        q->capacity = q->capacity ? q->capacity * 2 : 8;
        q->items = realloc(q->items, q->capacity * sizeof(Song *));
        if (q->shuffle_indices) {
            q->shuffle_indices = realloc(q->shuffle_indices, q->capacity * sizeof(size_t));
        }
    }
    q->items[q->count++] = song;

    if (q->shuffle_enabled) {
        queue_generate_shuffle(q);
    }
}

void queue_remove(SongQueue *q, Song *song)
{
    if (q->count == 0) return;

    size_t index = 0;
    int found = 0;
    for (size_t i = 0; i < q->count; i++) {
        if (q->items[i] == song) {
            index = i;
            found = 1;
            break;
        }
    }

    if (!found) return;

    for (size_t i = index; i < q->count - 1; i++) {
        q->items[i] = q->items[i + 1];
    }
    q->count--;

    if (q->count == 0) {
        q->current_selected = 0;
        q->current_playing = 0;
    } else {
        if (q->current_selected >= q->count) {
            q->current_selected = q->count - 1;
        } else if (q->current_selected > index) {
            q->current_selected--;
        }

        if (q->current_playing >= q->count) {
            q->current_playing = q->count - 1;
        } else if (q->current_playing > index) {
            q->current_playing--;
        }
    }

    if (q->shuffle_enabled) {
        queue_generate_shuffle(q);
    }
}

Song* queue_current_playing(SongQueue *q)
{
    if (q->count == 0) return NULL;

    return queue_get_at(q, q->current_playing);
}

Song* queue_current_selected(SongQueue *q)
{
    if (q->count == 0) return NULL;
    return q->items[q->current_selected];
}

Song* queue_play_next(SongQueue *q)
{
    if (q->count == 0) return NULL;

    if (q->loop_mode == LOOP_ONE) {
        return queue_current_playing(q);
    }

    q->current_playing = (q->current_playing + 1) % q->count;

    if (q->current_playing == 0 && q->loop_mode == LOOP_NONE) {
        q->current_playing = q->count - 1;
        return NULL;
    }

    return queue_get_at(q, q->current_playing);
}

Song* queue_play_prev(SongQueue *q)
{
    if (q->count == 0) return NULL;

    if (q->loop_mode == LOOP_ONE) {
        return queue_current_playing(q);
    }

    if (q->current_playing == 0) {
        if (q->loop_mode == LOOP_ALL) {
            q->current_playing = q->count - 1;
        } else {
            return NULL;
        }
    } else {
        q->current_playing--;
    }

    return queue_get_at(q, q->current_playing);
}

Song* queue_play(SongQueue *q, size_t index)
{
    if (q->count == 0 || index >= q->count) return NULL;
    q->current_playing = index;
    return queue_get_at(q, q->current_playing);
}

Song* queue_play_song(SongQueue *q, Song *song)
{
    if (q->count == 0 || !song) return NULL;

    for (size_t i = 0; i < q->count; i++) {
        if (q->items[i] == song) {
            if (q->shuffle_enabled && q->shuffle_indices) {
                for (size_t j = 0; j < q->count; j++) {
                    if (q->shuffle_indices[j] == i) {
                        q->current_playing = j;
                        return queue_get_at(q, q->current_playing);
                    }
                }
            } else {
                q->current_playing = i;
                return q->items[i];
            }
        }
    }

    return NULL;
}

Song* queue_select_next(SongQueue *q)
{
    if (q->count == 0) return NULL;
    q->current_selected = (q->current_selected + 1) % q->count;
    return q->items[q->current_selected];
}

Song* queue_select_prev(SongQueue *q)
{
    if (q->count == 0) return NULL;
    q->current_selected = (q->current_selected + q->count - 1) % q->count;
    return q->items[q->current_selected];
}

Song* queue_select(SongQueue *q, size_t index)
{
    if (q->count == 0 || index >= q->count) return NULL;
    q->current_selected = index;
    return q->items[q->current_selected];
}

Song* queue_select_song(SongQueue *q, Song *song)
{
    if (q->count == 0 || !song) return NULL;

    for (size_t i = 0; i < q->count; i++) {
        if (q->items[i] == song) {
            q->current_selected = i;
            return q->items[q->current_selected];
        }
    }

    return NULL;
}

void queue_set_shuffle(SongQueue *q, bool enabled)
{
    if (q->shuffle_enabled == enabled) return;

    // Remember which actual song is currently playing
    Song *currently_playing = NULL;
    if (q->count > 0) {
        currently_playing = queue_get_at(q, q->current_playing);
    }

    q->shuffle_enabled = enabled;

    if (enabled) {
        queue_generate_shuffle(q);

        if (currently_playing) {
            for (size_t i = 0; i < q->count; i++) {
                if (queue_get_at(q, i) == currently_playing) {
                    q->current_playing = i;
                    break;
                }
            }
        }
    } else {
        if (currently_playing) {
            for (size_t i = 0; i < q->count; i++) {
                if (q->items[i] == currently_playing) {
                    q->current_playing = i;
                    break;
                }
            }
        }
    }
}

void queue_set_loop_mode(SongQueue *q, LoopMode mode)
{
    q->loop_mode = mode;
}

bool queue_is_shuffle_enabled(SongQueue *q)
{
    return q->shuffle_enabled;
}

LoopMode queue_get_loop_mode(SongQueue *q)
{
    return q->loop_mode;
}
