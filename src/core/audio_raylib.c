#include "audio_backend.h"
#include <raylib.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Music music;
    bool loaded;
} RaylibAudio;

static bool rl_load(AudioBackend *ab, const char *path);
static void rl_play(AudioBackend *ab);
static void rl_pause(AudioBackend *ab);
static void rl_stop(AudioBackend *ab);
static void rl_seek(AudioBackend *ab, double seconds);
static double rl_position(AudioBackend *ab);
static bool rl_is_playing(AudioBackend *ab);
static void rl_update(AudioBackend *ab);
static void rl_resume(AudioBackend *ab);
static bool rl_is_loaded(AudioBackend *ab);
static bool rl_is_finished(AudioBackend *ab);
static double rl_length(AudioBackend *ab);

static AudioBackendVTable rl_vtable = {
    .load = rl_load,
    .play = rl_play,
    .pause = rl_pause,
    .stop = rl_stop,
    .seek = rl_seek,
    .position = rl_position,
    .is_playing = rl_is_playing,
    .update = rl_update,
    .resume = rl_resume,
    .is_loaded = rl_is_loaded,
    .is_finished = rl_is_finished,
    .get_length = rl_length,
};

struct AudioBackend* raylib_audio_backend_create(void)
{
    RaylibAudio *ra = calloc(1, sizeof(RaylibAudio));
    struct AudioBackend *ab = calloc(1, sizeof(struct AudioBackend));

    ab->vtable = &rl_vtable;
    ab->impl = ra;

    ra->loaded = false;

    return ab;
}

void raylib_audio_backend_destroy(struct AudioBackend *ab)
{
    if (!ab) return;

    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (ra->loaded) UnloadMusicStream(ra->music);
    free(ra);
    free(ab);
}

static bool rl_load(struct AudioBackend *ab, const char *path)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;

    if (ra->loaded)
        UnloadMusicStream(ra->music);

    ra->music = LoadMusicStream(path);
    ra->loaded = true;

    return ra->music.ctxData != NULL; // simple check
}

static void rl_play(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return;

    PlayMusicStream(ra->music);
}

static void rl_resume(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return;

    ResumeMusicStream(ra->music);
}

static void rl_pause(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return;

    PauseMusicStream(ra->music);
}

static void rl_stop(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return;

    StopMusicStream(ra->music);
}

static void rl_seek(struct AudioBackend *ab, double seconds)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return;

    SeekMusicStream(ra->music, (float)seconds);
}

static double rl_length(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return 0.0;

    return GetMusicTimeLength(ra->music);
}

static double rl_position(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return 0.0;

    return GetMusicTimePlayed(ra->music);
}

static bool rl_is_playing(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return false;

    return IsMusicStreamPlaying(ra->music);
}

static bool rl_is_loaded(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return false;

    return IsMusicValid(ra->music);
}

static bool rl_is_finished(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (ab->vtable->is_loaded) {
        if (GetMusicTimePlayed(ra->music) + 0.1 > GetMusicTimeLength(ra->music)) {
            return true;
        }
    }
    return false;
}

static void rl_update(struct AudioBackend *ab)
{
    RaylibAudio *ra = (RaylibAudio *)ab->impl;
    if (!ra->loaded) return;
    if (IsMusicValid(ra->music)) UpdateMusicStream(ra->music);
}
