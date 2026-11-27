#include "monitor.h"
#include <errno.h>

void mon_init(monitor_t *m)
{
    sem_init(&m->mutex, 1, 1);
    sem_init(&m->next,  1, 0);
    m->next_count = 0;
    m->ops = 0;
}

void cond_init(cond_t *c)
{
    sem_init(&c->sem, 1, 0);
    c->count = 0;
}

static void wait_retry(sem_t *s)
{
    while (sem_wait(s) == -1 && errno == EINTR)
        ;
}

void mon_enter(monitor_t *m)
{
    wait_retry(&m->mutex);
    m->ops++;
}

void mon_leave(monitor_t *m)
{
    m->ops++;
    if (m->next_count > 0)
        sem_post(&m->next);
    else
        sem_post(&m->mutex);
}

void cond_wait(monitor_t *m, cond_t *c)
{
    c->count++;

    m->ops++;
    if (m->next_count > 0)
        sem_post(&m->next);
    else
        sem_post(&m->mutex);

    wait_retry(&c->sem);
    m->ops++;

    c->count--;
}

void cond_signal(monitor_t *m, cond_t *c)
{
    if (c->count > 0) {
        m->next_count++;
        
        m->ops++;
        sem_post(&c->sem);
        
        wait_retry(&m->next);
        m->ops++;
        
        m->next_count--;
    }
}
