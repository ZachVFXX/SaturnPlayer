#pragma once
#include <stddef.h>
#define SUBPROCESS_IMPLEMENTATION
#include "../external/subprocess.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "utils/arena.h"

#define MAX_SEARCH_RESULTS 16

typedef struct {
    char* id;
    char* title;
    char* artist;
    char* duration;
    char* url;
} SearchResult;

typedef struct {
    SearchResult results[MAX_SEARCH_RESULTS];
    int count;
} SearchResults;


typedef struct {
    pthread_t thread;
    mem_arena* arena;
    char query[256];
    int max_results;

    SearchResults* results;
    bool done;
    bool success;
} Search;

typedef struct {
    pthread_t thread;
    mem_arena* arena;
    char url[512];
    char out_dir[512];

    char* final_path;
    bool done;
    bool success;
} Download;

static void* search_thread(void* arg) {
    Search* s = arg;

    char search_arg[512];
    snprintf(search_arg, sizeof(search_arg), "ytsearch%d:%s",
             s->max_results, s->query);

    const char* args[] = {
        "yt-dlp",
        "--print",
        "%(id)s|%(title)s|%(uploader)s|%(duration_string)s|%(webpage_url)s",
        "--skip-download",
        "--quiet",
        "--flat-playlist",
        "--no-playlist",
        search_arg,
        NULL
    };

    struct subprocess_s p;
    if (subprocess_create(args,
        subprocess_option_search_user_path, &p) != 0) {
        s->done = true;
        return NULL;
    }

    char buffer[4096];
    char* output = NULL;
    size_t size = 0;
    unsigned int n;

    while ((n = subprocess_read_stdout(&p, buffer, sizeof(buffer)-1)) > 0) {
        buffer[n] = 0;
        char* next = arena_push(s->arena, size + n + 1, false);
        if (output) memcpy(next, output, size);
        memcpy(next + size, buffer, n);
        size += n;
        next[size] = 0;
        output = next;
    }

    int ec;
    subprocess_join(&p, &ec);
    subprocess_destroy(&p);

    if (ec != 0 || !output) {
        s->done = true;
        return NULL;
    }

    SearchResults* r = PUSH_STRUCT(s->arena, SearchResults);
    r->count = 0;

    char* save_line;
    char* save_field;

    for (char* line = strtok_r(output, "\n", &save_line);
         line && r->count < s->max_results;
         line = strtok_r(NULL, "\n", &save_line)) {

        char* id       = strtok_r(line, "|", &save_field);
        char* title    = strtok_r(NULL, "|", &save_field);
        char* uploader = strtok_r(NULL, "|", &save_field);
        char* duration = strtok_r(NULL, "|", &save_field);
        char* url      = strtok_r(NULL, "|", &save_field);

        if (!id || !title || !uploader || !duration || !url)
            continue;

        SearchResult* e = &r->results[r->count++];
        e->id       = arena_strdup(s->arena, id);
        e->title    = arena_strdup(s->arena, title);
        e->artist   = arena_strdup(s->arena, uploader);
        e->duration = arena_strdup(s->arena, duration);
        e->url      = arena_strdup(s->arena, url);
    }

    s->results = r;
    s->success = true;
    s->done = true;
    return NULL;
}

static void* download_thread(void* arg) {
    Download* d = arg;
    // TODO: ADD OTHER TYPE LIKE SOUNDCLOUD
    // TODO: ADD WAY TO DOWNLOAD PLAYLIST TO
    const char* args[] = {
        "yt-dlp",
        "-x",
        "--audio-format", "mp3",
        "--audio-quality", "0",
        "--embed-metadata",
        "--embed-thumbnail",
        "--no-progress",
        "--print", "after_move:filepath",
        "--paths", d->out_dir,
        "-o", "%(id)s.%(ext)s",
        d->url,
        NULL
    };

    struct subprocess_s p;
    if (subprocess_create(args,
        subprocess_option_search_user_path, &p) != 0) {
        d->done = true;
        return NULL;
    }

    char buf[1024];
    unsigned int n;

    while ((n = subprocess_read_stdout(&p, buf, sizeof(buf)-1)) > 0) {
        buf[n] = 0;
        char* nl = strchr(buf, '\n');
        if (nl) *nl = 0;
        if (*buf)
            d->final_path = arena_strdup(d->arena, buf);
    }

    int ec;
    subprocess_join(&p, &ec);
    subprocess_destroy(&p);

    d->success = (ec == 0 && d->final_path);
    d->done = true;
    return NULL;
}

Search* search_start(mem_arena* arena, const char* query, int max_results) {
    Search* s = PUSH_STRUCT(arena, Search);
    memset(s, 0, sizeof(*s));

    s->arena = arena;
    s->max_results = max_results;
    strncpy(s->query, query, sizeof(s->query)-1);

    pthread_create(&s->thread, NULL, search_thread, s);
    return s;
}

bool search_done(Search* s) {
    if (!s) return false;
    if (!s->done) return false;
    return true;
}

SearchResults* search_results(Search* s) {
    return s->success ? s->results : NULL;
}

Download* download_start(mem_arena* arena, const char* url, const char* out_dir) {
    Download* d = PUSH_STRUCT(arena, Download);
    memset(d, 0, sizeof(*d));

    d->arena = arena;
    strncpy(d->url, url, sizeof(d->url)-1);
    strncpy(d->out_dir, out_dir, sizeof(d->out_dir)-1);

    pthread_create(&d->thread, NULL, download_thread, d);
    return d;
}

bool download_done(Download* d) {
    if (!d) return false;
    if (!d->done) return false;
    return true;
}
