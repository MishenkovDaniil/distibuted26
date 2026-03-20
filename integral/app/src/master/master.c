#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <pthread.h>

#include <log.h>
#include <net.h>
#include <task.h>
#include <parser.h>

#include "master.h"
#include "master_helpers.h"
#include "priority_queue.h"

static int master_sock_tcp = 0;

static int DISCOVERY_PORT = 4000;
static int MASTER_PORT = 6000;

static const int DISCOVERY_INTERVAL_USEC = 100000; // 100ms

static pthread_t discovery_thread;
static volatile int discovery_running = 0;

static const int TIMEOUT_SEC = 3;

static const int MAX_NODES = 100;

static char broadcast_addr[INET_ADDRSTRLEN] = "";

static int send_broadcast(int master_sock_udp)
{
    struct sockaddr_in bcast = { 0 };
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(DISCOVERY_PORT);

    if (broadcast_addr[0] != '\0') // docker
        inet_pton(AF_INET, broadcast_addr, &bcast.sin_addr);
    else // localhost
    {
        bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        inet_pton(AF_INET, "127.0.0.1", &bcast.sin_addr);
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &bcast.sin_addr, ip_str, sizeof(ip_str));
    DEBUG(Master, "broadcasting discovery message to %s:%d", ip_str, ntohs(bcast.sin_port));

    int msg = 1;
    ssize_t sent = sendto(master_sock_udp, (void *)&msg, sizeof(msg), 0,
                (struct sockaddr*)&bcast, sizeof(bcast));
    if (sent < 0)
    {
		ERROR(Master, "sendto discovery failed: %s", strerror(errno));
        close(master_sock_udp);
        return -1;
    }

    DEBUG(Master, "sent master node discovery message, bytes=%zd", sent);

    return 0;
}

static void master_shutdown()
{
    close(master_sock_tcp);
}

/* send_broadcast on udp socket while discovery flag is set */
static void *discovery_loop(void *arg)
{
    (void)arg;
    int master_sock_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (master_sock_udp < 0)
    {
		ERROR(Master, "socket failed: %s", strerror(errno));
        return NULL;
    }

    //set socket to do broadcast
    int yes = 1;
    setsockopt(master_sock_udp, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    while (discovery_running)
    {
        if (send_broadcast(master_sock_udp) < 0)
        {
            ERROR(Master, "discovery broadcast iteration failed");
        }

        usleep(DISCOVERY_INTERVAL_USEC);
    }

    close(master_sock_udp);

    return NULL;
}

static int start_discovery_thread(void)
{
    discovery_running = 1;
    int rc = pthread_create(&discovery_thread, NULL, discovery_loop, NULL);
    if (rc != 0)
    {
        discovery_running = 0;
        ERROR(Master, "failed to create discovery thread: %s", strerror(rc));
        return -1;
    }

    return 0;
}

static void stop_discovery_thread(void)
{
    if (!discovery_running)
        return;

    discovery_running = 0;
    pthread_join(discovery_thread, NULL);
}

/* Set up TCP socket to accept new connections */
static int accept_connections()
{
    master_sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (master_sock_tcp < 0)
    {
		ERROR(Master, "tcp socket failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in master = { 0 };
    master.sin_family = AF_INET;
    master.sin_port = htons(MASTER_PORT);
    master.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(master_sock_tcp, (struct sockaddr *)&master, sizeof(master)) < 0) {
		ERROR(Master, "bind tcp failed: %s", strerror(errno));
        return -1;
    }

    if (listen(master_sock_tcp, MAX_NODES) < 0) {
		ERROR(Master, "listen failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static void accept_new_conn(int epfd)
{
    struct sockaddr_in new_node_addr = { 0 };
    socklen_t new_node_addrlen = sizeof(new_node_addr);

    int new_node_sock = accept(master_sock_tcp, (struct sockaddr *)&new_node_addr, &new_node_addrlen);
    if (new_node_sock < 0)
    {
        ERROR(Master, "accept failed %s(%d)", strerror(errno), errno);
        return;
    }

    INFO(Master, "new worker node connected");

    struct epoll_event event = {0};
    event.events = EPOLLOUT | EPOLLHUP | EPOLLERR;
    event.data.ptr = epoll_event_data_ctor(new_node_sock, SIZE_MAX);

    epoll_ctl(epfd, EPOLL_CTL_ADD, new_node_sock, &event);
}

static void remove_dead_tasks(size_t *cur_task, pqueue_t *pqueue)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double now_dbl = timespec_to_double(now);

    /* Remove all tasks with expired deadlines (deadline <= now) */
    while (!pqueue_is_empty(pqueue) && pqueue_get_top_key(pqueue) <= now_dbl) {
        task_t *expired_task = (task_t *)pqueue_del_top(pqueue);
        if (expired_task != NULL) {
            DEBUG(Master, "task %zu timed out", expired_task->base.task_id);
            set_task_state(expired_task, TASK_PENDING);
            if (expired_task->base.task_id < *cur_task)
                *cur_task = expired_task->base.task_id;
        }
    }
}

static int master_routine(task_t *tasks, size_t tasks_cnt, int epfd, double *result)
{
    double sum = 0;
    double kahan_comp = 0;
    size_t ready_nodes = 0;
    size_t cur_task = 0;

    struct epoll_event master_sock_ev = { 0 };
    master_sock_ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    master_sock_ev.data.ptr = epoll_event_data_ctor(master_sock_tcp, SIZE_MAX);

    epoll_ctl(epfd, EPOLL_CTL_ADD, master_sock_tcp, &master_sock_ev);

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd < 0) {
        ERROR(Master, "timerfd_create failed: %s", strerror(errno));
        return -1;
    }

    if (timerfd_settime(tfd, 0, &(struct itimerspec){
        .it_interval = { .tv_sec = TIMEOUT_SEC, .tv_nsec = 0 },
        .it_value = { .tv_sec = TIMEOUT_SEC, .tv_nsec = 0 }
    }, NULL) < 0) {
        ERROR(Master, "timerfd_settime failed: %s", strerror(errno));
        close(tfd);
        return -1;
    }

    struct epoll_event timer_ev = { 0 };
    timer_ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    timer_ev.data.ptr = epoll_event_data_ctor(tfd, SIZE_MAX);
    pqueue_t *pqueue = pqueue_ctor();

    epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &timer_ev);

    while (1)
    {
        if (ready_nodes == tasks_cnt)
            break;

        struct epoll_event events[MAX_NODES];
        int ready_nodes_cnt = epoll_wait(epfd, events, MAX_NODES, 1000); //wait for max 1s
        if (ready_nodes_cnt < 0)
        {
            ERROR(Master, "epoll wait failed - %s(%d)", strerror(errno), errno);
            epoll_event_data_dtor((conn_info_t *)master_sock_ev.data.ptr);
            epoll_event_data_dtor((conn_info_t *)timer_ev.data.ptr);
            pqueue_dtor(pqueue);
            close(tfd);
            return -1;
        }

        for (int i = 0; i < ready_nodes_cnt; ++i)
        {
            conn_info_t *conn_info = (conn_info_t *)events[i].data.ptr;
            if (!conn_info)
            {
                ERROR(Master, "invalid epoll event data");
                continue;
            }

            /* event on timerfd */
            if(conn_info->fd == tfd)
            {
                if (events[i].events & EPOLLIN)
                {
                    DEBUG(Master, "EPOLLIN event on timerfd");
                    uint64_t expirations;
                    read(tfd, &expirations, sizeof(expirations));
                    remove_dead_tasks(&cur_task, pqueue);
                }
                else
                {
                    ERROR(Master, "error event(%d) on timerfd...", events[i].events);
                }
                continue;
            }

            int event_sock = conn_info->fd;
            const size_t task_id = conn_info->task_id;

            if(events[i].events & EPOLLIN)
            {
                /* event on master tcp socket (new worker connected)*/
                if (event_sock == master_sock_tcp)
                {
                    INFO(Master, "accept_new_conn");
                    accept_new_conn(epfd);
                }
                else /* event on worker socket */
                {
                    DEBUG(Master, "received task result from worker node");
                    answer_t ans;
                    if (recv_all(event_sock, &ans, sizeof(ans)) < 0)
                    {
                        ERROR(Master, "receive of task result failed: %s", strerror(errno));
                        if (task_id != SIZE_MAX && tasks[task_id].state == TASK_IN_PROGRESS) {
                            pqueue_del(pqueue, tasks + task_id, timespec_to_double(tasks[task_id].deadline));
                        }
                        remove_sock_and_requeue_task(epfd, event_sock, conn_info, tasks, task_id, &cur_task);
                        continue;
                    }

                    if (ans.task_id >= tasks_cnt)
                    {
                        ERROR(Master, "received invalid task id %zu", ans.task_id);
                        if (task_id != SIZE_MAX && tasks[task_id].state == TASK_IN_PROGRESS) {
                            pqueue_del(pqueue, tasks + task_id, timespec_to_double(tasks[task_id].deadline));
                        }
                        remove_sock_and_requeue_task(epfd, event_sock, conn_info, tasks, task_id, &cur_task);
                        continue;
                    }

                    if (tasks[ans.task_id].state != TASK_IN_PROGRESS ||
                        tasks[ans.task_id].worker_fd != event_sock ||
                        tasks[ans.task_id].base.execution_id != ans.execution_id)
                    {
                        DEBUG(Master, "received result for already completed task id %zu", ans.task_id);
                    }
                    else
                    {
                        pqueue_del(pqueue, tasks + ans.task_id, timespec_to_double(tasks[ans.task_id].deadline));
                        set_task_state(&tasks[ans.task_id], TASK_COMPLETED);
                        kahan_sum(&sum, &kahan_comp, ans.result);
                        ready_nodes++;
                    }

                    epoll_event_data_dtor(conn_info);

                    struct epoll_event event = {
                        .events = EPOLLOUT | EPOLLHUP | EPOLLERR,
                        .data.ptr = epoll_event_data_ctor(event_sock, SIZE_MAX)
                    };

                    epoll_ctl(epfd, EPOLL_CTL_MOD, event_sock, &event);
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                DEBUG(Master, "send task to worker node");

                skip_completed_tasks(&cur_task, tasks, tasks_cnt);
                if (cur_task >= tasks_cnt)
                    continue;

                tasks[cur_task].base.execution_id += 1;
                if (send_all(event_sock, &tasks[cur_task].base, sizeof(task_base_t)) < 0)
                {
                    ERROR(Master, "failed to send task to worker: %s", strerror(errno));
                    tasks[cur_task].base.execution_id -= 1;
                    remove_sock(epfd, event_sock, conn_info);
                    continue;
                }

                clock_gettime(CLOCK_MONOTONIC, &tasks[cur_task].deadline);
                tasks[cur_task].deadline.tv_sec += TIMEOUT_SEC;
                tasks[cur_task].worker_fd = event_sock;
                pqueue_add(pqueue, tasks + cur_task, timespec_to_double(tasks[cur_task].deadline));

                epoll_event_data_dtor(conn_info);

                struct epoll_event event = {
                    .events = EPOLLIN | EPOLLHUP | EPOLLERR,
                    .data.ptr = epoll_event_data_ctor(event_sock, cur_task)
                };
                epoll_ctl(epfd, EPOLL_CTL_MOD, event_sock, &event);

                set_task_state(&tasks[cur_task], TASK_IN_PROGRESS);
                cur_task++;
            }
            else if (events[i].events & (EPOLLHUP | EPOLLERR))
            {
                ERROR(Master, "EPOLLHUP event occured");
                if (task_id != SIZE_MAX && tasks[task_id].state == TASK_IN_PROGRESS) {
                    pqueue_del(pqueue, tasks + task_id, timespec_to_double(tasks[task_id].deadline));
                }
                remove_sock_and_requeue_task(epfd, event_sock, conn_info, tasks, task_id, &cur_task);
            }
            else
            {
                ERROR(Master, "unknown event occured, mask is %u", events[i].events);
            }
        }
    }

    epoll_event_data_dtor((conn_info_t *)master_sock_ev.data.ptr);
    epoll_event_data_dtor((conn_info_t *)timer_ev.data.ptr);
    pqueue_dtor(pqueue);
    close(tfd);

    *result = sum;
    return 0;
}

static int start_tcp(task_t *tasks, size_t tasks_cnt)
{
    INFO(Master, "waiting for worker nodes to connect...");
    int rc = 0;

    if (accept_connections() < 0)
    {
        return -1;
    }

    if (start_discovery_thread() < 0)
    {
		ERROR(Master, "failed to start discovery thread: %s", strerror(errno));
        master_shutdown();
        return -1;
    }

    int epfd = rc = epoll_create(MAX_NODES);
    if (epfd < 0)
    {
		ERROR(Master, "failed to create epoll fd: %s", strerror(errno));
        goto fail;
    }

    double integral_res;
    rc = master_routine(tasks, tasks_cnt, epfd, &integral_res);
    if (rc < 0)
    {
		ERROR(Master, "routine failed");
        close(epfd);
        goto fail;
    }

	INFO(Master, "result is %lf", integral_res);

    close(epfd);
fail:
    stop_discovery_thread();
    master_shutdown();
    return rc > 0 ? 0 : -1;
}

int main(const int argc, const char **argv)
{
    master_args_t args = {
        .master_port = MASTER_PORT,
        .discovery_port = DISCOVERY_PORT,
    };

    if (parse_master_args(argc, argv, &args) < 0)
    {
        ERROR(Master, "failed to parse master arguments");
        return -1;
    }

    MASTER_PORT = args.master_port;
    DISCOVERY_PORT = args.discovery_port;
    snprintf(broadcast_addr, sizeof(broadcast_addr), "%s", args.broadcast_addr);

    task_t *tasks = NULL;
    int tasks_cnt = prepare_tasks(&args.task, &tasks);
    if (tasks_cnt <= 0)
    {
		ERROR(Master, "failed to prepare tasks");
        return -1;
    }

    start_tcp(tasks, tasks_cnt);
    clear_tasks(&tasks);

    return 0;
}