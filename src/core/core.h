#ifndef CORE_H
#define CORE_H

#include <stdbool.h>
#include "song.h"
#include "queue.h"
#include "audio_backend.h"
#include "../utils/arena.h"

typedef struct Core Core;

typedef enum {
    CORE_STOPPED = 0,
    CORE_PLAYING,
    CORE_PAUSED
} CorePlaybackState;

typedef enum {
    CMD_QUEUE_ADD,
    CMD_PLAY,
    CMD_PLAY_SELECTED,      // Play the currently selected song
    CMD_PLAY_SONG,          // Play a specific song by pointer
    CMD_PLAY_INDEX,         // Play a specific song by index
    CMD_PAUSE,
    CMD_RESUME,
    CMD_STOP,
    CMD_PLAY_NEXT,
    CMD_PLAY_PREV,
    CMD_SELECT_NEXT,        // Select next song
    CMD_SELECT_PREV,        // Select previous song
    CMD_SELECT_INDEX,       // Select song by index
    CMD_SELECT_SONG,        // Select song by pointer
    CMD_SEEK_ABS,
    CMD_SEEK_REL,
    CMD_TOGGLE_SHUFFLE,
    CMD_SET_LOOP_MODE,
    CMD_QUEUE_REMOVE
} CoreCommandType;

typedef struct {
    CoreCommandType type;
    union {
        Song *song;
        double seek_seconds;
        size_t index;
        bool shuffle_enabled;
        LoopMode loop_mode;
    };
} CoreCommand;

Core *core_create(AudioBackend *audio, mem_arena* string_arena);
void  core_start(Core *core);
void  core_stop(Core *core);
void  core_destroy(Core *core);

void core_send_command(Core *core, CoreCommand cmd);

CorePlaybackState core_get_state(Core *core);
Song *core_get_current_song_playing(Core *core);
Song *core_get_current_song_selected(Core *core);

size_t core_get_queue_count(Core *core);
Song *core_get_queue_item(Core *core, size_t index);
bool core_is_shuffle_enabled(Core *core);
LoopMode core_get_loop_mode(Core *core);
Song* core_get_song_at(Core *core, size_t index);

double core_get_audio_position(Core *core);
double core_get_audio_length(Core *core);
bool   core_is_audio_loaded(Core *core);

#endif
