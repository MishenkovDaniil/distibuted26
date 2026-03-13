#ifndef INTEGRAL_WORKER_H
#define INTEGRAL_WORKER_H

#include <base.h>

struct worker
{
    int discovery_socket;
    int task_socket;
    bool is_alive;
};

typedef struct worker worker_t;

int start_worker();
int worker_routine(worker_t *worker, struct sockaddr_in *master_addr);
void complete_task (integral_task_t *task);
int shutdown_worker(worker_t *worker);
void parse_env();

#endif /* INTEGRAL_WORKER_H */
