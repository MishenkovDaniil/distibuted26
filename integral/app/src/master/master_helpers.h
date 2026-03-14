#ifndef MASTER_HELPERS_H
#define MASTER_HELPERS_H

#include <stdlib.h>

#include <log.h>
#include <task.h>

#include "master.h"

conn_info_t *epoll_event_data_ctor(int fd, size_t task_id);
void epoll_event_data_dtor(conn_info_t *info);
void requeue_task(task_t *tasks, size_t task_id, size_t *cur_task_id);
void add_sock_to_epoll(int epfd, int op, int sock_fd, int events, size_t task_id);
void remove_sock(int epfd, int sock_fd, conn_info_t *conn_info);
void remove_sock_and_requeue_task(int epfd, int sock_fd, conn_info_t *conn_info, task_t *tasks, size_t task_id, size_t *cur_task_id);

static inline void skip_completed_tasks(size_t *cur_task_id, task_t *tasks, size_t tasks_cnt)
{
    while (*cur_task_id < tasks_cnt && get_task_state(&tasks[*cur_task_id]) != TASK_PENDING)
        (*cur_task_id)++;
}

#endif /* MASTER_HELPERS_H */
