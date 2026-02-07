#pragma once

#include <stdint.h>

typedef uint32_t str_id;

typedef struct {
    str_id path;
    str_id title;
    str_id artists;
    str_id album;
    float length;
    uint16_t textureIndex;
    uint16_t id;
} Song;
