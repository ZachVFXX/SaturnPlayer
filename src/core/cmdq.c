#include "cmdq.h"

void cmdq_init(CommandQueue *q)
{
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

void cmdq_push(CommandQueue *q, CoreCommand cmd)
{
    pthread_mutex_lock(&q->mutex);

    if (q->count < CMDQ_CAPACITY) {
        q->items[q->tail] = cmd;
        q->tail = (q->tail + 1) % CMDQ_CAPACITY;
        q->count++;
        pthread_cond_signal(&q->cond);
    }

    pthread_mutex_unlock(&q->mutex);
}

bool cmdq_pop(CommandQueue* restrict q, CoreCommand* restrict out)
{
    pthread_mutex_lock(&q->mutex);

    if (q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    *out = q->items[q->head];
    q->head = (q->head + 1) % CMDQ_CAPACITY;
    q->count--;

    pthread_mutex_unlock(&q->mutex);
    return true;
}
