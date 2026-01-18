#ifndef MPRIS_INTERFACE_H
#define MPRIS_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MPRIS_STOPPED,
    MPRIS_PLAYING,
    MPRIS_PAUSED
} PlaybackStatus;

typedef enum {
    MPRIS_LOOP_NONE,
    MPRIS_LOOP_TRACK,
    MPRIS_LOOP_PLAYLIST
} LoopStatus;

typedef struct {
    void (*on_next)();
    void (*on_previous)();
    void (*on_pause)();
    void (*on_play)();
    void (*on_play_pause)();
    void (*on_stop)();
    // Seeking
    void (*on_seek)(long long offset_usec);
    void (*on_set_position)(const char* track_id, long long pos_usec);
    // Loop/Shuffle
    void (*on_set_loop)(LoopStatus status);
    void (*on_set_shuffle)(bool enabled);
    // Getters for sync
    double (*get_position)(); // Should return GetMusicTimePlayed(music)
    double (*get_duration)(); // Should return GetMusicTimeLength(music)
} MPRISCallbacks;

void mpris_init(MPRISCallbacks callbacks);
void mpris_set_status(PlaybackStatus status);
void mpris_emit_seeked(int64_t position_usec);
void mpris_update_position(int64_t position_usec);
void mpris_update_metadata(const char* title, const char* artist, const char* album, const char* art_url);
void mpris_start_thread(MPRISCallbacks cbs);

#endif
