#pragma once
#include <stdbool.h>

struct AudioBackend;

typedef struct {
    bool (*load)(AudioBackend *ab, const char *path);
    void (*play)(AudioBackend *ab);
    void (*pause)(AudioBackend *ab);
    void (*resume)(AudioBackend *ab);
    void (*stop)(AudioBackend *ab);
    void (*seek)(AudioBackend *ab, double seconds);
    double (*position)(AudioBackend *ab);
    bool (*is_playing)(AudioBackend *ab);
    bool (*is_loaded)(AudioBackend *ab);
    bool (*is_finished)(AudioBackend *ab);
    void (*update)(AudioBackend *ab);
} AudioBackendVTable;

struct AudioBackend {
    AudioBackendVTable *vtable;
    void *impl;
};
