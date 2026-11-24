#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <semaphore.h>
#include <time.h>
#include "monitor.h"

#define MAX_CHEFS 16
#define MAX_ITEMS 32
#define MAX_K 3
#define WATCHDOG_MS 1000
#define WATCHDOG_NS 1000000000LL
#define THINK_MAX_US 2000
#define COOK_US 2000
#define SYSV_KEY 0x4B495443
#define SHM_NAME "/cse325_g4_kitchen"

typedef enum {
    MODE_NOSYNC,
    MODE_NAIVE,
    MODE_ORDERED,
    MODE_ROOM,
    MODE_SYSV,
    MODE_MONITOR,
    MODE_COUNT
} kmode_t;

enum {
    CHEF_THINKING,
    CHEF_HUNGRY,
    CHEF_COOKING
};

typedef struct {
    kmode_t mode;
    int chefs;
    int k;
    int items;
    double seconds;
    long max_dishes;
    unsigned int seed;
    double reach_ms;
    int verbose;
    char csv[256];
    char experiment[32];
    char param_name[32];
    char param_value[32];
    int run;
} config_t;

typedef struct {
    int state;
    int waiting_for;
    int holds[MAX_K];
    long dishes;
    long violations;
    long sem_ops;
    long long wait_total_ns;
    long long wait_max_ns;
    long long last_meal_ns;
} chef_t;

typedef struct {
    config_t cfg;
    long long start_ns;
    int need[MAX_CHEFS][MAX_K];
    int owner[MAX_ITEMS];
    chef_t chef[MAX_CHEFS];
    sem_t item[MAX_ITEMS];
    sem_t room;
    int sysv_id;
    monitor_t mon;
    cond_t self[MAX_CHEFS];
    int mon_state[MAX_CHEFS];
} kitchen_t;

typedef struct {
    double elapsed_s;
    long total_dishes;
    int deadlock;
    double deadlock_after_s;
} run_result_t;

static inline long long now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline int sem_wait_retry(sem_t *s)
{
    int r;

    do {
        r = sem_wait(s);
    } while (r == -1 && errno == EINTR);
    return r;
}

const char *mode_name(kmode_t m);
int mode_parse(const char *s, kmode_t *m);

void ipc_remove_stale(void);
kitchen_t *ipc_create(const config_t *cfg);
void ipc_destroy(kitchen_t *k);

void chef_run(kitchen_t *k, int id, int dish_fd);

int stats_report(const kitchen_t *k, const run_result_t *r);

#endif
