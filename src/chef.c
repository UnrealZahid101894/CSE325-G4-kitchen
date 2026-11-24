#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "common.h"

void chef_run(kitchen_t *k, int id, int dish_fd)
{
    struct timespec ts = {0, 5000000L};
    sigset_t term, old;
    char b = 'x';

    sigemptyset(&term);
    sigaddset(&term, SIGTERM);

    for (;;) {
        nanosleep(&ts, NULL);
        sigprocmask(SIG_BLOCK, &term, &old);
        if (write(dish_fd, &b, 1) != 1)
            _exit(1);
        k->chef[id].dishes++;
        sigprocmask(SIG_SETMASK, &old, NULL);
    }
}
