#include <errno.h>
#include <math.h>
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
#include <pthread.h>

#include <log.h>
#include <parser.h>

#include "master.h"

static int master_sock_tcp = 0;

static int DISCOVERY_PORT = 4000;
static int MASTER_PORT = 6000;

static const int DISCOVERY_INTERVAL_USEC = 100000; // 100ms

static pthread_t discovery_thread;
static volatile int discovery_running = 0;

static const double DELTA = 0.01;
static const int MAX_NODES = 100;

int send_broadcast(int master_sock_udp)
{
    struct sockaddr_in bcast = { 0 };
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(DISCOVERY_PORT);
    bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);// 255.255.255.255

    inet_pton(AF_INET, "127.0.0.1", &bcast.sin_addr);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &bcast.sin_addr, ip_str, sizeof(ip_str));
    INFO(Master, "broadcasting discovery message to %s:%d", ip_str, ntohs(bcast.sin_port));

    int msg = 1;
    ssize_t sent = sendto(master_sock_udp, (void *)&msg, sizeof(msg), 0,
                (struct sockaddr*)&bcast, sizeof(bcast));
    if (sent < 0)
    {
		ERROR(Master, "sendto discovery failed: %s", strerror(errno));
        close(master_sock_udp);
        return -1;
    }

    INFO(Master, "sent master node discovery message, bytes=%zd", sent);

    return 0;
}

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

int start_tcp(integral_task_t *tasks, size_t tasks_cnt)
{
    INFO(Master, "waiting for worker nodes to connect...");

    if (accept_connections() < 0)
        return -1;

    if (start_discovery_thread() < 0)
    {
        close(master_sock_tcp);
        return -1;
    }

    int epfd = epoll_create(MAX_NODES);
    if (epfd < 0)
    {
		ERROR(Master, "failed to create epoll fd: %s", strerror(errno));
        stop_discovery_thread();
        close(master_sock_tcp);
        return -1;
    }

    double integral_res;
    int rc = master_routine(tasks, tasks_cnt, epfd, &integral_res);
    if (rc < 0)
    {
		ERROR(Master, "master routine failed");
        close(epfd);
        stop_discovery_thread();
        master_shutdown();
        return -1;
    }
	INFO(Master, "result is %lf", integral_res);

    close(epfd);
    stop_discovery_thread();
    master_shutdown();
    return rc;
}

int accept_connections()
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

    epoll_data_t new_node_data = {
        .fd = new_node_sock,
    };

    struct epoll_event event = {0};
    event.events = EPOLLOUT | EPOLLHUP | EPOLLERR;
    event.data = new_node_data;

    epoll_ctl(epfd, EPOLL_CTL_ADD, new_node_sock, &event);
}
int master_routine(integral_task_t *tasks, size_t tasks_cnt, int epfd, double *result)
{
    double sum = 0;
    size_t ready_nodes = 0;
    size_t cur_task = 0;

    struct epoll_event event1 = { 0 };
    event1.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    event1.data.fd = master_sock_tcp;

    epoll_ctl(epfd, EPOLL_CTL_ADD, master_sock_tcp, &event1);

    while (1)
    {
        if (ready_nodes == tasks_cnt)
            break;

        struct epoll_event events[MAX_NODES];
        int ready_nodes_cnt = epoll_wait(epfd, events, MAX_NODES, 1000); //wait for max 1s
        if (ready_nodes_cnt < 0)
        {
            ERROR(Master, "epoll wait failed - %s(%d)", strerror(errno), errno);
            return -1;
        }

        for (int i = 0; i < ready_nodes_cnt; ++i)
        {
            if(events[i].events & EPOLLIN)
            {
                int new_node_sock = events[i].data.fd;
                if (new_node_sock == master_sock_tcp)
                {
                    INFO(Master, "accept_new_conn");
                    accept_new_conn(epfd);
                }
                else
                {
                    INFO(Master, "received task result from worker node");
                    integral_task_t task;
                    if (recv(new_node_sock, &task, sizeof(task), 0) < 0)
                    {
                        ERROR(Master, "receive of task result failed: %s", strerror(errno));
                        continue;
                    }

                    sum += task.result;

                    struct epoll_event event = { 0 };
                    event.events = EPOLLOUT | EPOLLHUP | EPOLLERR;
                    event.data = events[i].data;

                    epoll_ctl(epfd, EPOLL_CTL_MOD, new_node_sock, &event);
                    ready_nodes++;
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                INFO(Master, "send task to worker node");
                if (cur_task >= tasks_cnt)
                    continue;

                int new_node_sock = events[i].data.fd;
                if (send(new_node_sock, tasks + cur_task, sizeof(integral_task_t), 0) < 0)
                {
                    ERROR(Master, "failed to send task to worker: %s", strerror(errno));
                    continue;
                }

                struct epoll_event event = { 0 };
                event.events = EPOLLIN | EPOLLHUP | EPOLLERR;
                event.data = events[i].data;

                epoll_ctl(epfd, EPOLL_CTL_MOD, new_node_sock, &event);
                cur_task++;
            }
            else if (events[i].events & EPOLLHUP)
            {
                ERROR(Master, "EPOLLHUP event occured");
            }
            else if (events[i].events & EPOLLERR)
            {
                ERROR(Master, "EPOLLERR event occured");
            }
            else
            {
                ERROR(Master, "unknown event occured, mask is %u", events[i].events);
            }
        }
    }

    *result = sum;
    return 0;
}

void master_shutdown()
{
    close(master_sock_tcp);
}

ssize_t prepare_tasks(integral_task_t *task, integral_task_t **tasks)
{
    if (task->right <= task->left)
        return -1;

    size_t tasks_cnt = (size_t)ceil((task->right - task->left) / DELTA);
    if (tasks_cnt == 0)
        return -1;

    *tasks = (integral_task_t *)malloc(sizeof(integral_task_t) * tasks_cnt);
    if (!*tasks)
        return -1;

    double left = task->left;
    for (size_t i = 0; i < tasks_cnt; ++i)
    {
        double right = left + DELTA;
        if (right > task->right)
            right = task->right;

        (*tasks)[i].function = NULL;
        (*tasks)[i].result = 0.0;
        (*tasks)[i].left = left;
        (*tasks)[i].right = right;

        left = right;
    }

    return tasks_cnt;
}

void clear_tasks(integral_task_t *tasks)
{
    free(tasks);
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

    integral_task_t *tasks = NULL;
    int tasks_cnt = prepare_tasks(&args.task, &tasks);
    if (tasks_cnt <= 0)
    {
		ERROR(Master, "failed to prepare tasks");
        return -1;
    }

    start_tcp(tasks, tasks_cnt);
    clear_tasks(tasks);

    return 0;
}