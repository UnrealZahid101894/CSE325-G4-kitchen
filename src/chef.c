#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/sem.h>
#include <sys/types.h>
#include "common.h"

static void nap_ns(long long ns)
{
    struct timespec ts;

    if (ns <= 0)
        return;
    ts.tv_sec = ns / 1000000000LL;
    ts.tv_nsec = ns % 1000000000LL;
    nanosleep(&ts, NULL);
}

static void reach_pause(const kitchen_t *k)
{
    nap_ns((long long)(k->cfg.reach_ms * 1e6));
}

static void say(const kitchen_t *k, int id, const char *what, int item)
{
    double t;

    if (!k->cfg.verbose)
        return;
    t = (double)(now_ns() - k->start_ns) / 1e9;
    if (item >= 0)
        printf("%8.3f chef %d %s item %d\n", t, id, what, item);
    else
        printf("%8.3f chef %d %s\n", t, id, what);
}

static void claim(kitchen_t *k, int id, int j, int item)
{
    chef_t *c = &k->chef[id];

    if (k->owner[item] != -1)
        c->violations++;
    k->owner[item] = id;
    c->holds[j] = item;
}

static void drop_claims(kitchen_t *k, int id)
{
    chef_t *c = &k->chef[id];
    int j, item;

    for (j = 0; j < k->cfg.k; j++) {
        item = c->holds[j];
        if (item < 0)
            continue;
        if (k->owner[item] == id)
            k->owner[item] = -1;
        c->holds[j] = -1;
    }
}

static void take_one(kitchen_t *k, int id, int j, int item)
{
    chef_t *c = &k->chef[id];

    c->waiting_for = item;
    sem_wait_retry(&k->item[item]);
    c->sem_ops++;
    c->waiting_for = -1;
    claim(k, id, j, item);
    say(k, id, "takes", item);
}

static void take_all(kitchen_t *k, int id, const int *order)
{
    int j;

    for (j = 0; j < k->cfg.k; j++) {
        if (j > 0)
            reach_pause(k);
        take_one(k, id, j, order[j]);
    }
}

static void take_unprotected(kitchen_t *k, int id, const int *order)
{
    int j;

    for (j = 0; j < k->cfg.k; j++) {
        if (j > 0)
            reach_pause(k);
        claim(k, id, j, order[j]);
        say(k, id, "takes", order[j]);
    }
}

static void give_all(kitchen_t *k, int id, const int *order)
{
    chef_t *c = &k->chef[id];
    int j;

    for (j = k->cfg.k - 1; j >= 0; j--) {
        sem_post(&k->item[order[j]]);
        c->sem_ops++;
    }
}

static void sysv_op(kitchen_t *k, int id, const int *order, int delta)
{
    struct sembuf ops[MAX_K];
    int j;

    for (j = 0; j < k->cfg.k; j++) {
        ops[j].sem_num = (unsigned short)order[j];
        ops[j].sem_op = (short)delta;
        ops[j].sem_flg = 0;
    }
    while (semop(k->sysv_id, ops, (size_t)k->cfg.k) == -1 && errno == EINTR) {
    }
    k->chef[id].sem_ops++;
}

static int shares(const kitchen_t *k, int a, int b)
{
    int x, y;

    for (x = 0; x < k->cfg.k; x++)
        for (y = 0; y < k->cfg.k; y++)
            if (k->need[a][x] == k->need[b][y])
                return 1;
    return 0;
}

static void mon_test(kitchen_t *k, int me, int i)
{
    int j;

    if (k->mon_state[i] != CHEF_HUNGRY)
        return;
    for (j = 0; j < k->cfg.chefs; j++)
        if (j != i && k->mon_state[j] == CHEF_COOKING && shares(k, i, j))
            return;
    k->mon_state[i] = CHEF_COOKING;
    cond_signal(&k->mon, &k->self[i]);
    k->chef[me].sem_ops++;
}

static void mon_pickup(kitchen_t *k, int id)
{
    chef_t *c = &k->chef[id];

    mon_enter(&k->mon);
    c->sem_ops++;
    k->mon_state[id] = CHEF_HUNGRY;
    mon_test(k, id, id);
    if (k->mon_state[id] != CHEF_COOKING) {
        cond_wait(&k->mon, &k->self[id]);
        c->sem_ops++;
    }
    mon_leave(&k->mon);
    c->sem_ops++;
}

static void mon_putdown(kitchen_t *k, int id)
{
    chef_t *c = &k->chef[id];
    int j;

    mon_enter(&k->mon);
    c->sem_ops++;
    k->mon_state[id] = CHEF_THINKING;
    for (j = 0; j < k->cfg.chefs; j++)
        if (j != id && shares(k, id, j))
            mon_test(k, id, j);
    mon_leave(&k->mon);
    c->sem_ops++;
}

static void acquire(kitchen_t *k, int id, const int *order)
{
    chef_t *c = &k->chef[id];
    int j;

    switch (k->cfg.mode) {
    case MODE_NOSYNC:
        take_unprotected(k, id, order);
        break;
    case MODE_NAIVE:
    case MODE_ORDERED:
        take_all(k, id, order);
        break;
    case MODE_ROOM:
        sem_wait_retry(&k->room);
        c->sem_ops++;
        take_all(k, id, order);
        break;
    case MODE_SYSV:
        c->waiting_for = order[0];
        sysv_op(k, id, order, -1);
        c->waiting_for = -1;
        for (j = 0; j < k->cfg.k; j++)
            claim(k, id, j, order[j]);
        say(k, id, "takes all items", -1);
        break;
    case MODE_MONITOR:
        mon_pickup(k, id);
        for (j = 0; j < k->cfg.k; j++)
            claim(k, id, j, order[j]);
        say(k, id, "takes all items", -1);
        break;
    default:
        break;
    }
}

static void release(kitchen_t *k, int id, const int *order)
{
    drop_claims(k, id);
    switch (k->cfg.mode) {
    case MODE_NAIVE:
    case MODE_ORDERED:
        give_all(k, id, order);
        break;
    case MODE_ROOM:
        give_all(k, id, order);
        sem_post(&k->room);
        k->chef[id].sem_ops++;
        break;
    case MODE_SYSV:
        sysv_op(k, id, order, 1);
        break;
    case MODE_MONITOR:
        mon_putdown(k, id);
        break;
    default:
        break;
    }
}

static void sort_order(int *order, int n)
{
    int i, j, t;

    for (i = 1; i < n; i++) {
        t = order[i];
        for (j = i - 1; j >= 0 && order[j] > t; j--)
            order[j + 1] = order[j];
        order[j + 1] = t;
    }
}

void chef_run(kitchen_t *k, int id, int dish_fd)
{
    chef_t *c = &k->chef[id];
    unsigned int seed = k->cfg.seed + 7919u * (unsigned int)(id + 1);
    int order[MAX_K];
    sigset_t term, old;
    long long t0, w;
    char b = 'x';
    int j;

    sigemptyset(&term);
    sigaddset(&term, SIGTERM);

    for (j = 0; j < k->cfg.k; j++)
        order[j] = k->need[id][j];
    if (k->cfg.mode == MODE_ORDERED || k->cfg.mode == MODE_SYSV)
        sort_order(order, k->cfg.k);

    for (;;) {
        c->state = CHEF_THINKING;
        nap_ns((long long)(rand_r(&seed) % (THINK_MAX_US + 1)) * 1000LL);

        c->state = CHEF_HUNGRY;
        say(k, id, "is hungry", -1);
        t0 = now_ns();
        acquire(k, id, order);
        w = now_ns() - t0;
        c->wait_total_ns += w;
        if (w > c->wait_max_ns)
            c->wait_max_ns = w;

        c->state = CHEF_COOKING;
        say(k, id, "cooks", -1);
        nap_ns((long long)COOK_US * 1000LL);

        release(k, id, order);

        sigprocmask(SIG_BLOCK, &term, &old);
        if (write(dish_fd, &b, 1) != 1)
            _exit(1);
        c->dishes++;
        c->last_meal_ns = now_ns();
        sigprocmask(SIG_SETMASK, &old, NULL);
        say(k, id, "finished a dish", -1);
    }
}
