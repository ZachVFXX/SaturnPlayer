#include "vector.h"
#include <stdlib.h>
#include <string.h>


// Vector functions
int vectorInit(Vector* vec, size_t element_size, size_t init_capacity) {
    vec->element_size = element_size;
    vec->count = 0;
    vec->capacity = init_capacity > 0 ? init_capacity : 16;
    vec->data = malloc(vec->capacity * vec->element_size);
    return vec->data ? 0 : -1;
}

void vectorFree(Vector* vec) {
    if (vec->data) {
        free(vec->data);
        vec->data = NULL;
    }
    vec->capacity = 0;
    vec->count = 0;
    vec->element_size = 0;
}

int vectorEnsureCapacity(Vector* vec, size_t min_capacity) {
    if (min_capacity <= vec->capacity) return 0;

    size_t new_capacity = vec->capacity;
    while (new_capacity < min_capacity) {
        new_capacity *= 2;
    }

    void* new_data = realloc(vec->data, new_capacity * vec->element_size);
    if (!new_data) return -1;

    vec->data = new_data;
    vec->capacity = new_capacity;
    return 0;
}

int vectorAppend(Vector* vec, const void* elem) {
    if (vectorEnsureCapacity(vec, vec->count + 1) != 0) return -1;
    memcpy((char*)vec->data + vec->count * vec->element_size, elem, vec->element_size);
    vec->count++;
    return 0;
}

int vectorInsert(Vector* vec, size_t index, const void* elem) {
    if (index > vec->count) return -1;
    if (vectorEnsureCapacity(vec, vec->count + 1) != 0) return -1;

    if (index < vec->count) {
        void* dest = (char*)vec->data + (index + 1) * vec->element_size;
        void* src = (char*)vec->data + index * vec->element_size;
        memmove(dest, src, (vec->count - index) * vec->element_size);
    }

    memcpy((char*)vec->data + index * vec->element_size, elem, vec->element_size);
    vec->count++;
    return 0;
}

int vectorRemove(Vector* vec, size_t index) {
    if (index >= vec->count) return -1;

    if (index < vec->count - 1) {
        void* dest = (char*)vec->data + index * vec->element_size;
        void* src = (char*)vec->data + (index + 1) * vec->element_size;
        memmove(dest, src, (vec->count - index - 1) * vec->element_size);
    }

    vec->count--;
    return 0;
}

void* vectorGet(Vector* vec, size_t index) {
    if (index >= vec->count) return NULL;
    return (char*)vec->data + index * vec->element_size;
}

int vectorFindIndex(Vector* vec, void* data) {
    if (!data) return -1;
        for (size_t i = 0; i < vec->count; i++) {
            if (vectorGet(vec, i) == data) {
                return i;
            }
        }
    return -1;
}

int vectorShuffle(Vector* vec) {
    if (!vec || !vec->data || vec->count == 0) return -1;

    // Fisher-Yates shuffle algorithm
    for (size_t i = vec->count - 1; i > 0; i--) {
        // Generate random index from 0 to i (inclusive)
        size_t j = rand() % (i + 1);

        if (i != j) {
            // Swap elements at positions i and j
            void* temp = malloc(vec->element_size);
            if (!temp) return -1;

            void* elem_i = (char*)vec->data + i * vec->element_size;
            void* elem_j = (char*)vec->data + j * vec->element_size;

            memcpy(temp, elem_i, vec->element_size);
            memcpy(elem_i, elem_j, vec->element_size);
            memcpy(elem_j, temp, vec->element_size);

            free(temp);
        }
    }

    return 0;
}
