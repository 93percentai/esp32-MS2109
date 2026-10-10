#pragma once
#include <pthread.h>
#include <stdlib.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *lock = malloc(sizeof(*lock));
    if (lock) pthread_mutex_init(lock, NULL);
    return lock;
}
static inline int xSemaphoreTake(SemaphoreHandle_t lock, unsigned ticks)
{ (void)ticks; return pthread_mutex_lock(lock); }
static inline int xSemaphoreGive(SemaphoreHandle_t lock) { return pthread_mutex_unlock(lock); }
