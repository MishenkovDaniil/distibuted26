#ifndef INTEGRAL_MASTER_H
#define INTEGRAL_MASTER_H

#include <base.h>

int send_broadcast();
int start_tcp(integral_task_t *tasks, size_t tasks_cnt);
int accept_connections();
int master_routine(integral_task_t *tasks, size_t tasks_cnt, int epfd, double *result);
void master_shutdown();

ssize_t prepare_tasks(integral_task_t *task, integral_task_t **tasks);
void clear_tasks(integral_task_t *tasks);

#endif /* INTEGRAL_MASTER_H */
