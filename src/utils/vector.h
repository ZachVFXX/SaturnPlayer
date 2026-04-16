#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>

typedef struct {
    void* data;
    size_t count;
    size_t capacity;
    size_t element_size;
} Vector;

int vectorInit(Vector* vec, size_t element_size, size_t init_capacity);

void vectorFree(Vector* vec);

int vectorEnsureCapacity(Vector* vec, size_t min_capacity);

int vectorAppend(Vector* vec, const void* elem);

int vectorInsert(Vector* vec, size_t index, const void* elem);

int vectorRemove(Vector* vec, size_t index);

void* vectorGet(Vector* vec, size_t index);

int vectorShuffle(Vector* vec);

int vectorFindIndex(Vector* vec, void* data);

#endif // VECTOR_H
