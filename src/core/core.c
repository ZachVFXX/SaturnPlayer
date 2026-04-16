#include "../utils/arena.h"
#include "core.h"
#include "queue.c"
#include "cmdq.c"
#include "audio_backend.h"
#include "queue.h"
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdbool.h>

struct Core {
    pthread_t thread;
    bool running;
    pthread_mutex_t mutex;
    CorePlaybackState state;
    SongQueue queue;
    CommandQueue cmdq;
    AudioBackend *audio;
    mem_arena* string_arena;
};

static void core_check_song_ended(Core *c);

static void core_play_current(Core *c)
{
    Song *song = queue_current_playing(&c->queue);
    if (!song)
        return;

    const char* song_path = arena_get_string(c->string_arena, song->path);
    c->audio->vtable->load(c->audio, song_path);
    c->audio->vtable->play(c->audio);
    c->state = CORE_PLAYING;
}

static void core_play_selected(Core *c)
{
    Song *song = queue_current_selected(&c->queue);
    if (!song) {
        TraceLog(LOG_WARNING, "Can't play song selected because its NULL.");
        return;
    }
    // Set the selected song as the currently playing song
    queue_play_song(&c->queue, song);

    const char* song_path = arena_get_string(c->string_arena, song->path);
    c->audio->vtable->load(c->audio, song_path);
    c->audio->vtable->play(c->audio);
    c->state = CORE_PLAYING;
}

static void core_handle_command(Core *c, CoreCommand *cmd)
{
    switch (cmd->type) {
    case CMD_QUEUE_ADD:
        queue_push(&c->queue, cmd->song);
        break;
    case CMD_PLAY:
        if (c->state != CORE_PLAYING)
            core_play_current(c);
        break;
    case CMD_PLAY_SELECTED:
        core_play_selected(c);
        break;
    case CMD_PLAY_SONG:
        if (queue_play_song(&c->queue, cmd->song))
            core_play_current(c);
        break;
    case CMD_PLAY_INDEX:
        if (queue_play(&c->queue, cmd->index))
            core_play_current(c);
        break;
    case CMD_PAUSE:
        if (c->state == CORE_PLAYING) {
            c->audio->vtable->pause(c->audio);
            c->state = CORE_PAUSED;
        }
        break;
    case CMD_RESUME:
        if (c->state == CORE_PAUSED) {
            c->audio->vtable->resume(c->audio);
            c->state = CORE_PLAYING;
        }
        break;
    case CMD_STOP:
        c->audio->vtable->stop(c->audio);
        c->state = CORE_STOPPED;
        break;
    case CMD_PLAY_NEXT:
        if (queue_play_next(&c->queue))
            core_play_current(c);
        break;
    case CMD_PLAY_PREV:
        if (queue_play_prev(&c->queue))
            core_play_current(c);
        break;
    case CMD_SELECT_NEXT:
        queue_select_next(&c->queue);
        break;
    case CMD_SELECT_PREV:
        queue_select_prev(&c->queue);
        break;
    case CMD_SELECT_INDEX:
        queue_select(&c->queue, cmd->index);
        break;
    case CMD_SELECT_SONG:
        queue_select_song(&c->queue, cmd->song);
        break;
    case CMD_SEEK_ABS:
        c->audio->vtable->seek(c->audio, cmd->seek_seconds);
        break;
    case CMD_SEEK_REL:
        if ((c->audio->vtable->position(c->audio) + cmd->seek_seconds) > c->audio->vtable->get_length(c->audio)) {
            if (queue_play_next(&c->queue))
                core_play_current(c);
            break;
        } else if ((c->audio->vtable->position(c->audio) + cmd->seek_seconds) < 0.0) {
            if (queue_play_prev(&c->queue))
                core_play_current(c);
            break;
        }
        c->audio->vtable->seek(c->audio, (c->audio->vtable->position(c->audio) + cmd->seek_seconds));
        break;
    case CMD_TOGGLE_SHUFFLE:
        queue_set_shuffle(&c->queue, cmd->shuffle_enabled);
        break;
    case CMD_SET_LOOP_MODE:
        queue_set_loop_mode(&c->queue, cmd->loop_mode);
        break;
    case CMD_QUEUE_REMOVE:
        if (queue_current_playing(&c->queue) == cmd->song) {
            core_send_command(c, (CoreCommand) { .type = CMD_STOP });
        }
        queue_remove(&c->queue, cmd->song);
        break;
    default:
        break;
    }
}

static void *core_thread_fn(void *arg)
{
    Core *c = arg;
    CoreCommand cmd;

    while (c->running) {
        // Process all pending commands
        while (cmdq_pop(&c->cmdq, &cmd)) {
            pthread_mutex_lock(&c->mutex);
            TraceLog(LOG_INFO, "[CORE] Handling command %d.", cmd.type);
            core_handle_command(c, &cmd);
            pthread_mutex_unlock(&c->mutex);
        }

        // Tick audio backend
        c->audio->vtable->update(c->audio);
        // Check if song ended
        core_check_song_ended(c);

        struct timespec ts = {0, 5 * 1000 * 1000}; // 5ms
        nanosleep(&ts, NULL);
    }

    return NULL;
}

Core *core_create(AudioBackend *audio, mem_arena* string_arena)
{
    Core *c = calloc(1, sizeof(Core));
    if (!c)
        return NULL;

    c->audio = audio;
    c->state = CORE_STOPPED;
    c->running = false;
    c->string_arena = string_arena;
    pthread_mutex_init(&c->mutex, NULL);
    queue_init(&c->queue);
    cmdq_init(&c->cmdq);

    return c;
}

void core_start(Core *c)
{
    if (c->running)
        return;
    c->running = true;
    pthread_create(&c->thread, NULL, core_thread_fn, c);
}

void core_stop(Core *c)
{
    if (!c->running)
        return;
    c->running = false;
    pthread_join(c->thread, NULL);
}

Song* core_get_song_at(Core *c, size_t index)
{
    pthread_mutex_lock(&c->mutex);
    Song *song = queue_get_at(&c->queue, index);
    pthread_mutex_unlock(&c->mutex);
    return song;
}

void core_destroy(Core *c)
{
    if (!c)
        return;
    core_stop(c);
    queue_free(&c->queue);
    pthread_mutex_destroy(&c->mutex);
    free(c);
}

void core_send_command(Core *c, CoreCommand cmd)
{
    cmdq_push(&c->cmdq, cmd);
}

CorePlaybackState core_get_state(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    CorePlaybackState state = c->state;
    pthread_mutex_unlock(&c->mutex);
    return state;
}

Song *core_get_current_song_playing(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    Song *song = queue_current_playing(&c->queue);
    pthread_mutex_unlock(&c->mutex);
    return song;
}

Song *core_get_current_song_selected(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    Song *song = queue_current_selected(&c->queue);
    pthread_mutex_unlock(&c->mutex);
    return song;
}

size_t core_get_queue_count(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    size_t count = c->queue.count;
    pthread_mutex_unlock(&c->mutex);
    return count;
}

Song *core_get_queue_item(Core *c, size_t index)
{
    pthread_mutex_lock(&c->mutex);
    Song *song = NULL;
    if (index < c->queue.count) {
        song = c->queue.items[index];
    }
    pthread_mutex_unlock(&c->mutex);
    return song;
}

static void core_check_song_ended(Core *c)
{
    if (c->audio->vtable->is_finished(c->audio)) {
        Song *next = queue_play_next(&c->queue);
        if (next) {
            core_play_current(c);
        } else {
            // Queue ended (no loop)
            c->audio->vtable->stop(c->audio);
            c->state = CORE_STOPPED;
        }
    }
}

bool core_is_shuffle_enabled(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    bool enabled = queue_is_shuffle_enabled(&c->queue);
    pthread_mutex_unlock(&c->mutex);
    return enabled;
}

LoopMode core_get_loop_mode(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    LoopMode mode = queue_get_loop_mode(&c->queue);
    pthread_mutex_unlock(&c->mutex);
    return mode;
}

double core_get_audio_position(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    double pos = c->audio->vtable->position(c->audio);
    pthread_mutex_unlock(&c->mutex);
    return pos;
}

double core_get_audio_length(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    double len = c->audio->vtable->get_length(c->audio);
    pthread_mutex_unlock(&c->mutex);
    return len;
}

bool core_is_audio_loaded(Core *c)
{
    pthread_mutex_lock(&c->mutex);
    bool loaded = c->audio->vtable->is_loaded(c->audio);
    pthread_mutex_unlock(&c->mutex);
    return loaded;
}
