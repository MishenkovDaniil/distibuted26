#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <log.h>
#include <task.h>

#include "master_helpers.h"

conn_info_t *epoll_event_data_ctor(int fd, size_t task_id)
{
    conn_info_t *new_node_info = malloc(sizeof(conn_info_t));
    if (!new_node_info)
    {
        ERROR(Master, "failed to allocate memory for new node info");
        return NULL;
    }

    new_node_info->fd = fd;
    new_node_info->task_id = task_id;
    return new_node_info;
}

void epoll_event_data_dtor(conn_info_t *info)
{
    free(info);
}

void requeue_task(task_t *tasks, size_t task_id, size_t *cur_task_id)
{
    if (task_id == SIZE_MAX)
        return;

    task_t *task = tasks + task_id;
    if (get_task_state(task) == TASK_PENDING ||
        get_task_state(task) == TASK_COMPLETED)
        return;

    set_task_state(task, TASK_PENDING);
    if (task->base.task_id < *cur_task_id)
        *cur_task_id = task->base.task_id;
}

void add_sock_to_epoll(int epfd, int op, int sock_fd, int events, size_t task_id)
{
    struct epoll_event event = {0};
    event.events = events;
    event.data.ptr = epoll_event_data_ctor(sock_fd, task_id);

    epoll_ctl(epfd, op, sock_fd, &event);
}

void remove_sock(int epfd, int sock_fd, conn_info_t *conn_info)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, sock_fd, NULL);
    close(sock_fd);
    epoll_event_data_dtor(conn_info);
}

void remove_sock_and_requeue_task(int epfd, int sock_fd, conn_info_t *conn_info, task_t *tasks, size_t task_id, size_t *cur_task_id)
{
    remove_sock(epfd, sock_fd, conn_info);
    requeue_task (tasks, task_id, cur_task_id);
}
