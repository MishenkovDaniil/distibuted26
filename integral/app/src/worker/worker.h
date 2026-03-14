#ifndef INTEGRAL_WORKER_H
#define INTEGRAL_WORKER_H

#include <base.h>

typedef struct worker
{
    int discovery_socket;
    int task_socket;
    bool is_alive;
} worker_t;

int start_worker();
int worker_routine(worker_t *worker, struct sockaddr_in *master_addr);
int shutdown_worker(worker_t *worker);
void parse_env();

#endif /* INTEGRAL_WORKER_H */
