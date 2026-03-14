#ifndef INTEGRAL_MASTER_H
#define INTEGRAL_MASTER_H

#include <base.h>

int send_broadcast(int master_sock_udp);
int accept_connections();
void master_shutdown();

int start_tcp(task_t *tasks, size_t tasks_cnt);
int master_routine(task_t *tasks, size_t tasks_cnt, int epfd, double *result);

#endif /* INTEGRAL_MASTER_H */
