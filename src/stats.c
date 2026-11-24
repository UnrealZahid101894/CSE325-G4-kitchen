#include <stdio.h>
#include "common.h"

int stats_report(const kitchen_t *k, const run_result_t *r)
{
    long sum = 0;
    int i;

    for (i = 0; i < k->cfg.chefs; i++) {
        printf("chef %d dishes=%ld\n", i, k->chef[i].dishes);
        sum += k->chef[i].dishes;
    }
    printf("elapsed=%.3f total=%ld sum=%ld deadlock=%d\n",
           r->elapsed_s, r->total_dishes, sum, r->deadlock);
    if (sum == r->total_dishes) {
        printf("CHECK counts: PASS\n");
        return 0;
    }
    printf("CHECK counts: FAIL\n");
    return 1;
}
