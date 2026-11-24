#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [options]\n"
        "  --mode=NAME          nosync | naive | ordered | room | sysv | monitor (default ordered)\n"
        "  --seconds=T          run time in seconds (default 5)\n"
        "  --items=N            stop after N dishes are cooked\n"
        "  --seed=S             random seed (default 325)\n"
        "  --csv=FILE           append one result row to FILE\n"
        "  --verbose            print every event\n"
        "  --chefs=N            number of chefs, 2 to %d (default 5)\n"
        "  --reach-ms=X         pause between taking items in ms (default 1)\n"
        "  --items-per-dish=K   2 or 3 (default 2)\n"
        "  --experiment=NAME    experiment label for the CSV row\n"
        "  --param-name=NAME    parameter name for the CSV row\n"
        "  --param-value=VALUE  parameter value for the CSV row\n"
        "  --run=R              run number for the CSV row\n",
        prog, MAX_CHEFS);
}

static int parse_long(const char *s, long *out)
{
    char *end;

    errno = 0;
    *out = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0')
        return -1;
    return 0;
}

static int parse_double(const char *s, double *out)
{
    char *end;

    errno = 0;
    *out = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0')
        return -1;
    return 0;
}

static int parse_args(int argc, char **argv, config_t *cfg)
{
    static const struct option opts[] = {
        {"mode", required_argument, NULL, 'm'},
        {"seconds", required_argument, NULL, 's'},
        {"items", required_argument, NULL, 'i'},
        {"seed", required_argument, NULL, 'd'},
        {"csv", required_argument, NULL, 'c'},
        {"verbose", no_argument, NULL, 'v'},
        {"chefs", required_argument, NULL, 'n'},
        {"reach-ms", required_argument, NULL, 'r'},
        {"items-per-dish", required_argument, NULL, 'k'},
        {"experiment", required_argument, NULL, 'e'},
        {"param-name", required_argument, NULL, 'p'},
        {"param-value", required_argument, NULL, 'q'},
        {"run", required_argument, NULL, 'u'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int c, seconds_set = 0;
    long v;
    double d;

    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = MODE_ORDERED;
    cfg->chefs = 5;
    cfg->k = 2;
    cfg->seconds = 5.0;
    cfg->seed = 325;
    cfg->reach_ms = 1.0;
    cfg->run = 1;
    snprintf(cfg->experiment, sizeof(cfg->experiment), "%s", "manual");
    snprintf(cfg->param_name, sizeof(cfg->param_name), "%s", "none");
    snprintf(cfg->param_value, sizeof(cfg->param_value), "%s", "0");

    while ((c = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (c) {
        case 'm':
            if (mode_parse(optarg, &cfg->mode) != 0)
                return -1;
            break;
        case 's':
            if (parse_double(optarg, &d) != 0 || d <= 0)
                return -1;
            cfg->seconds = d;
            seconds_set = 1;
            break;
        case 'i':
            if (parse_long(optarg, &v) != 0 || v <= 0)
                return -1;
            cfg->max_dishes = v;
            break;
        case 'd':
            if (parse_long(optarg, &v) != 0 || v < 0)
                return -1;
            cfg->seed = (unsigned int)v;
            break;
        case 'c':
            snprintf(cfg->csv, sizeof(cfg->csv), "%s", optarg);
            break;
        case 'v':
            cfg->verbose = 1;
            break;
        case 'n':
            if (parse_long(optarg, &v) != 0)
                return -1;
            cfg->chefs = (int)v;
            break;
        case 'r':
            if (parse_double(optarg, &d) != 0 || d < 0)
                return -1;
            cfg->reach_ms = d;
            break;
        case 'k':
            if (parse_long(optarg, &v) != 0)
                return -1;
            cfg->k = (int)v;
            break;
        case 'e':
            snprintf(cfg->experiment, sizeof(cfg->experiment), "%s", optarg);
            break;
        case 'p':
            snprintf(cfg->param_name, sizeof(cfg->param_name), "%s", optarg);
            break;
        case 'q':
            snprintf(cfg->param_value, sizeof(cfg->param_value), "%s", optarg);
            break;
        case 'u':
            if (parse_long(optarg, &v) != 0)
                return -1;
            cfg->run = (int)v;
            break;
        default:
            return -1;
        }
    }
    if (optind < argc)
        return -1;
    if (cfg->max_dishes > 0 && !seconds_set)
        cfg->seconds = 0;
    if (cfg->chefs < 2 || cfg->chefs > MAX_CHEFS)
        return -1;
    if (cfg->k != 2 && cfg->k != 3)
        return -1;
    return 0;
}

static int holder_of(const kitchen_t *k, int c)
{
    int w = k->chef[c].waiting_for;

    if (w < 0 || w >= k->cfg.items)
        return -1;
    return k->owner[w];
}

static void report_deadlock(const kitchen_t *k)
{
    int n = k->cfg.chefs;
    int i, j, cur, steps, len, first;
    int path[MAX_CHEFS + 1];

    printf("DEADLOCK: no dish finished for %d ms\n", WATCHDOG_MS);
    for (i = 0; i < n; i++) {
        printf("  chef %d holds", i);
        first = 1;
        for (j = 0; j < k->cfg.k; j++) {
            if (k->chef[i].holds[j] >= 0) {
                printf("%s item %d", first ? "" : ",", k->chef[i].holds[j]);
                first = 0;
            }
        }
        if (first)
            printf(" nothing");
        if (k->chef[i].waiting_for >= 0) {
            printf(" and waits for item %d", k->chef[i].waiting_for);
            if (holder_of(k, i) >= 0)
                printf(" (held by chef %d)", holder_of(k, i));
        } else {
            printf(" and is not waiting for an item");
        }
        printf("\n");
    }

    for (i = 0; i < n; i++) {
        len = 0;
        cur = i;
        path[len++] = cur;
        for (steps = 0; steps < n; steps++) {
            cur = holder_of(k, cur);
            if (cur < 0)
                break;
            path[len++] = cur;
            if (cur == i) {
                printf("  wait-for cycle:");
                for (j = 0; j < len; j++)
                    printf(" chef %d%s", path[j], j + 1 < len ? " ->" : "");
                printf("\n");
                return;
            }
        }
    }
    printf("  no wait-for cycle found\n");
}

int main(int argc, char **argv)
{
    config_t cfg;
    kitchen_t *k;
    pid_t pids[MAX_CHEFS];
    pid_t p;
    struct sigaction sa;
    struct pollfd pfd;
    run_result_t res;
    char buf[4096];
    ssize_t got;
    long long start, last, end, deadline, t, limit;
    long total = 0;
    int fds[2];
    int i, n, started = 0, status = 0;
    int deadlock = 0, failed = 0, interrupted = 0, fails = 0, code = 0;
    int timeout_ms;

    if (parse_args(argc, argv, &cfg) != 0) {
        usage(argv[0]);
        return 1;
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    k = ipc_create(&cfg);
    if (k == NULL)
        return 1;
    n = k->cfg.chefs;

    if (pipe(fds) == -1) {
        perror("pipe");
        ipc_destroy(k);
        return 1;
    }

    printf("kitchen: mode=%s chefs=%d items=%d per-dish=%d reach=%gms seed=%u\n",
           mode_name(k->cfg.mode), n, k->cfg.items, k->cfg.k, k->cfg.reach_ms,
           k->cfg.seed);

    start = now_ns();
    k->start_ns = start;
    for (i = 0; i < n; i++)
        k->chef[i].last_meal_ns = start;

    for (i = 0; i < n && !g_stop; i++) {
        p = fork();
        if (p < 0) {
            perror("fork");
            failed = 1;
            break;
        }
        if (p == 0) {
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_IGN);
            close(fds[0]);
            chef_run(k, i, fds[1]);
            _exit(0);
        }
        pids[started++] = p;
    }
    close(fds[1]);

    last = start;
    deadline = (k->cfg.seconds > 0) ? start + (long long)(k->cfg.seconds * 1e9) : 0;

    while (!g_stop && !failed) {
        t = now_ns();
        if (deadline > 0 && t >= deadline)
            break;
        if (k->cfg.max_dishes > 0 && total >= k->cfg.max_dishes)
            break;
        if (t - last >= WATCHDOG_NS) {
            deadlock = 1;
            break;
        }
        limit = last + WATCHDOG_NS;
        if (deadline > 0 && deadline < limit)
            limit = deadline;
        timeout_ms = (int)((limit - t + 999999) / 1000000);

        pfd.fd = fds[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, timeout_ms) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            failed = 1;
            break;
        }
        if (pfd.revents & (POLLIN | POLLHUP)) {
            got = read(fds[0], buf, sizeof(buf));
            if (got > 0) {
                total += (long)got;
                last = now_ns();
            } else if (got == 0) {
                fprintf(stderr, "all chefs exited unexpectedly\n");
                failed = 1;
            } else if (errno != EINTR) {
                perror("read");
                failed = 1;
            }
        }
    }
    end = now_ns();
    interrupted = g_stop ? 1 : 0;

    if (deadlock)
        report_deadlock(k);

    for (i = 0; i < started; i++)
        kill(pids[i], SIGTERM);
    for (i = 0; i < started; i++) {
        while (waitpid(pids[i], &status, 0) == -1 && errno == EINTR) {
        }
    }
    while ((got = read(fds[0], buf, sizeof(buf))) > 0)
        total += (long)got;
    close(fds[0]);

    memset(&res, 0, sizeof(res));
    res.elapsed_s = (double)(end - start) / 1e9;
    res.total_dishes = total;
    res.deadlock = deadlock;
    res.deadlock_after_s = deadlock ? (double)(last - start) / 1e9 : -1.0;

    if (failed) {
        code = 1;
    } else if (interrupted) {
        printf("interrupted: cleaning up\n");
        code = 130;
    } else {
        fails = stats_report(k, &res);
        code = deadlock ? 2 : (fails ? 1 : 0);
    }

    ipc_destroy(k);
    return code;
}
