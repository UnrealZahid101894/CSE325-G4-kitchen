#ifndef MONITOR_H
#define MONITOR_H

#include <semaphore.h>

typedef struct {
    sem_t mutex;
    sem_t next;
    int next_count;
    long ops;
} monitor_t;

typedef struct {
    sem_t sem;
    int count;
} cond_t;

void mon_init(monitor_t *m);
void mon_enter(monitor_t *m);
void mon_leave(monitor_t *m);
void cond_init(cond_t *c);
void cond_wait(monitor_t *m, cond_t *c);
void cond_signal(monitor_t *m, cond_t *c);

#endif
