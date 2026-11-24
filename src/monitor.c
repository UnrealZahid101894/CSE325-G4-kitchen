#include "monitor.h"

void mon_init(monitor_t *m)
{
    sem_init(&m->mutex, 1, 1);
    sem_init(&m->next, 1, 0);
    m->next_count = 0;
}

void cond_init(cond_t *c)
{
    sem_init(&c->sem, 1, 0);
    c->count = 0;
}
