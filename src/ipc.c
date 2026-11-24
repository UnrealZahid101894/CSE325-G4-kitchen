#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include "common.h"

union ksemun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static const char *const mode_names[MODE_COUNT] = {
    "nosync", "naive", "ordered", "room", "sysv", "monitor"
};

const char *mode_name(kmode_t m)
{
    if ((int)m < 0 || (int)m >= MODE_COUNT)
        return "unknown";
    return mode_names[m];
}

int mode_parse(const char *s, kmode_t *m)
{
    int i;

    for (i = 0; i < MODE_COUNT; i++) {
        if (strcmp(s, mode_names[i]) == 0) {
            *m = (kmode_t)i;
            return 0;
        }
    }
    return -1;
}

void ipc_remove_stale(void)
{
    int id;

    shm_unlink(SHM_NAME);
    id = semget(SYSV_KEY, 0, 0);
    if (id != -1)
        semctl(id, 0, IPC_RMID);
}

static kitchen_t *fail_create(kitchen_t *k, const char *what)
{
    perror(what);
    ipc_destroy(k);
    return NULL;
}

kitchen_t *ipc_create(const config_t *cfg)
{
    kitchen_t *k;
    union ksemun arg;
    unsigned short vals[MAX_ITEMS];
    int fd, i, j, n, m;

    ipc_remove_stale();

    fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        perror("shm_open");
        return NULL;
    }
    if (ftruncate(fd, (off_t)sizeof(kitchen_t)) == -1) {
        perror("ftruncate");
        close(fd);
        shm_unlink(SHM_NAME);
        return NULL;
    }
    k = mmap(NULL, sizeof(kitchen_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    shm_unlink(SHM_NAME);
    if (k == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    memset(k, 0, sizeof(kitchen_t));
    k->sysv_id = -1;
    k->cfg = *cfg;

    n = cfg->chefs;
    m = (cfg->k == 3) ? 2 * n : n;
    k->cfg.items = m;

    for (i = 0; i < n; i++) {
        for (j = 0; j < cfg->k; j++)
            k->need[i][j] = (cfg->k == 3) ? (2 * i + j) % m : (i + j) % m;
        k->chef[i].waiting_for = -1;
        for (j = 0; j < MAX_K; j++)
            k->chef[i].holds[j] = -1;
        k->chef[i].state = CHEF_THINKING;
        k->mon_state[i] = CHEF_THINKING;
    }

    for (i = 0; i < m; i++) {
        k->owner[i] = -1;
        if (sem_init(&k->item[i], 1, 1) == -1)
            return fail_create(k, "sem_init item");
    }
    if (sem_init(&k->room, 1, (unsigned int)(n - 1)) == -1)
        return fail_create(k, "sem_init room");

    mon_init(&k->mon);
    for (i = 0; i < n; i++)
        cond_init(&k->self[i]);

    if (cfg->mode == MODE_SYSV) {
        k->sysv_id = semget(SYSV_KEY, m, IPC_CREAT | IPC_EXCL | 0600);
        if (k->sysv_id == -1)
            return fail_create(k, "semget");
        for (i = 0; i < m; i++)
            vals[i] = 1;
        arg.array = vals;
        if (semctl(k->sysv_id, 0, SETALL, arg) == -1)
            return fail_create(k, "semctl SETALL");
    }

    return k;
}

void ipc_destroy(kitchen_t *k)
{
    if (k == NULL)
        return;
    if (k->sysv_id != -1)
        semctl(k->sysv_id, 0, IPC_RMID);
    shm_unlink(SHM_NAME);
    munmap(k, sizeof(kitchen_t));
}
