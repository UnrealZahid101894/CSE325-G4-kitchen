#include <stdio.h>
#include "common.h"

typedef struct {
    long sum_dishes;
    long min_dishes;
    long max_dishes;
    long violations;
    long sem_ops;
    long long wait_total_ns;
    long long wait_max_ns;
    double fairness;
    double throughput;
    double avg_wait_ms;
    double max_wait_ms;
    double ops_per_dish;
} summary_t;

static void summarize(const kitchen_t *k, const run_result_t *r, summary_t *s)
{
    int i;
    const chef_t *c;

    s->sum_dishes = 0;
    s->violations = 0;
    s->sem_ops = 0;
    s->wait_total_ns = 0;
    s->wait_max_ns = 0;
    s->min_dishes = k->chef[0].dishes;
    s->max_dishes = k->chef[0].dishes;

    for (i = 0; i < k->cfg.chefs; i++) {
        c = &k->chef[i];
        s->sum_dishes += c->dishes;
        s->violations += c->violations;
        s->sem_ops += c->sem_ops;
        s->wait_total_ns += c->wait_total_ns;
        if (c->wait_max_ns > s->wait_max_ns)
            s->wait_max_ns = c->wait_max_ns;
        if (c->dishes < s->min_dishes)
            s->min_dishes = c->dishes;
        if (c->dishes > s->max_dishes)
            s->max_dishes = c->dishes;
    }

    s->fairness = (s->max_dishes > 0) ? (double)s->min_dishes / (double)s->max_dishes : 0.0;
    s->throughput = (r->elapsed_s > 0) ? (double)r->total_dishes / r->elapsed_s : 0.0;
    s->avg_wait_ms = (s->sum_dishes > 0)
        ? (double)s->wait_total_ns / (double)s->sum_dishes / 1e6 : 0.0;
    s->max_wait_ms = (double)s->wait_max_ns / 1e6;
    s->ops_per_dish = (s->sum_dishes > 0) ? (double)s->sem_ops / (double)s->sum_dishes : 0.0;
}

static void print_table(const kitchen_t *k)
{
    int i;
    const chef_t *c;
    double avg;

    printf("chef   dishes  avg_wait_ms  max_wait_ms\n");
    for (i = 0; i < k->cfg.chefs; i++) {
        c = &k->chef[i];
        avg = (c->dishes > 0) ? (double)c->wait_total_ns / (double)c->dishes / 1e6 : 0.0;
        printf("%4d %8ld %12.3f %12.3f\n", i, c->dishes, avg, (double)c->wait_max_ns / 1e6);
    }
}

static int write_csv(const kitchen_t *k, const run_result_t *r, const summary_t *s)
{
    const config_t *c = &k->cfg;
    FILE *f;

    f = fopen(c->csv, "a");
    if (f == NULL) {
        perror("csv");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) {
        fprintf(f, "group,experiment,mode,param_name,param_value,run,elapsed_s,ops,"
                   "throughput_ops_s,avg_wait_ms,max_wait_ms,violations,fairness,"
                   "min_dishes,max_dishes,deadlock_after_s,sem_ops_per_dish,deadlocked\n");
    }
    fprintf(f, "4,%s,%s,%s,%s,%d,%.3f,%ld,%.1f,%.3f,%.3f,%ld,%.3f,%ld,%ld,%.3f,%.2f,%d\n",
            c->experiment, mode_name(c->mode), c->param_name, c->param_value, c->run,
            r->elapsed_s, r->total_dishes, s->throughput, s->avg_wait_ms, s->max_wait_ms,
            s->violations, s->fairness, s->min_dishes, s->max_dishes,
            r->deadlock_after_s, s->ops_per_dish, r->deadlock);
    fclose(f);
    return 0;
}

int stats_report(const kitchen_t *k, const run_result_t *r)
{
    summary_t s;
    int fails = 0;
    int i, hungry = 0, first_hungry = -1;

    summarize(k, r, &s);

    print_table(k);
    printf("total=%ld dishes in %.3f s, throughput=%.1f dishes/s\n",
           r->total_dishes, r->elapsed_s, s.throughput);
    printf("fairness=%.3f (min=%ld, max=%ld), avg_wait=%.3f ms, max_wait=%.3f ms\n",
           s.fairness, s.min_dishes, s.max_dishes, s.avg_wait_ms, s.max_wait_ms);

    if (s.violations == 0) {
        printf("CHECK exclusive: PASS\n");
    } else {
        printf("CHECK exclusive: FAIL (%ld violations: an item had two owners)\n", s.violations);
        fails++;
    }

    if (!r->deadlock) {
        printf("CHECK no_deadlock: PASS\n");
    } else {
        printf("CHECK no_deadlock: FAIL (watchdog fired, last dish at %.3f s)\n",
               r->deadlock_after_s);
        fails++;
    }

    for (i = 0; i < k->cfg.chefs; i++) {
        if (k->chef[i].dishes < 1) {
            hungry++;
            if (first_hungry < 0)
                first_hungry = i;
        }
    }
    if (hungry == 0) {
        printf("CHECK all_ate: PASS\n");
    } else {
        printf("CHECK all_ate: FAIL (%d chefs cooked nothing, first is chef %d)\n",
               hungry, first_hungry);
        fails++;
    }

    if (s.sum_dishes == r->total_dishes) {
        printf("CHECK counts: PASS\n");
    } else {
        printf("CHECK counts: FAIL (per-chef sum %ld, coordinator total %ld)\n",
               s.sum_dishes, r->total_dishes);
        fails++;
    }

    if (k->cfg.csv[0] != '\0')
        write_csv(k, r, &s);

    return fails;
}
