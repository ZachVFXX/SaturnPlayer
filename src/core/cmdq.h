#ifndef CMQD_H
#define CMQD_H

#include <pthread.h>
#include <stdbool.h>
#include "core.h"

#define CMDQ_CAPACITY 64

typedef struct {
    CoreCommand items[CMDQ_CAPACITY];
    int head, tail, count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} CommandQueue;

void cmdq_init(CommandQueue *q);
void cmdq_push(CommandQueue *q, CoreCommand cmd);
bool cmdq_pop(CommandQueue *q, CoreCommand *out);

#endif
