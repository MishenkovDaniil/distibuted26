#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#include <log.h>
#include <parser.h>

#include "worker.h"

static int DISCOVERY_PORT = 4000;
static int MASTER_PORT = 6000;

// static const int ready_message = 0xDEADBEEF;
int main(const int argc, const char **argv)
{
    worker_args_t args = {
        .master_port = MASTER_PORT,
        .discovery_port = DISCOVERY_PORT,
    };

    if (parse_worker_args(argc, argv, &args) < 0)
    {
        ERROR(Worker, "failed to parse worker arguments");
        return -1;
    }

    MASTER_PORT = args.master_port;
    DISCOVERY_PORT = args.discovery_port;

    worker_t *worker = malloc(sizeof(worker_t));
    if (!worker)
    {
		ERROR(Worker, "worker creation failed: %s", strerror(errno));
        return -1;
    }

    start_worker(worker);

    free(worker);
    return 0;
}

int start_worker(worker_t *worker)
{
    worker->discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    worker->is_alive = false;

    int opt = 1;
    setsockopt(worker->discovery_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server = { 0 };
    server.sin_family = AF_INET;
    server.sin_port = htons(DISCOVERY_PORT);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    char listen_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server.sin_addr, listen_ip, sizeof(listen_ip));
	DEBUG(Worker, "binding UDP discovery socket to %s:%d", listen_ip, ntohs(server.sin_port));

    if (bind(worker->discovery_socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
		ERROR(Worker, "bind discovery failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in master_addr;
    DEBUG(Worker, "waiting for master node discovery message...");
    for (;;)
    {
        int discovery_msg = -1;
        socklen_t master_addr_len = sizeof(master_addr);

        int rc = recvfrom(worker->discovery_socket, (void *)&discovery_msg, sizeof(discovery_msg),
                            0, (struct sockaddr *)&master_addr, &master_addr_len);
        if (rc < 0)
        {
			ERROR(Worker, "recvfrom failed: %s", strerror(errno));
            continue;
        }

        char master_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &master_addr.sin_addr, master_ip, sizeof(master_ip));
        DEBUG(Worker, "received UDP packet from %s:%d, bytes=%d, msg=%d",
               master_ip, ntohs(master_addr.sin_port), rc, discovery_msg);
        if (discovery_msg != 1)
        {
            ERROR(Worker, "received bad discovery message [%d] from master node", discovery_msg);
            continue;
        }
        DEBUG(Worker, "received master node discovery message");
        master_addr.sin_port = htons(MASTER_PORT);
        break;
    }

    close(worker->discovery_socket);
    return worker_routine(worker, &master_addr);
}

static double my_func(double a)
{
    return a;
}

static void complete_task (task_base_t *task, answer_t *ans)
{
    double left = task->left;
    double right = task->right;

    ans->result = (right - left) * task->function((right + left) / 2);;
    ans->task_id = task->task_id;
}

int worker_routine(worker_t *worker, struct sockaddr_in *master_addr)
{
    int rc;

    worker->discovery_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (worker->discovery_socket < 0)
    {
		ERROR(Worker, "tcp socket failed: %s", strerror(errno));
        return -1;
    }

    rc = connect(worker->discovery_socket, (struct sockaddr*)master_addr, sizeof(*master_addr));
    if (rc < 0)
    {
		ERROR(Worker, "connect failed: %s", strerror(errno));
        return -1;
    }

	INFO(Worker, "connected to master node");

    for (;;)
    {
        task_base_t task;
        if (recv(worker->discovery_socket, &task, sizeof(task), 0) < 0)
        {
			ERROR(Worker, "recv failed: %s", strerror(errno));
            return -1;
        }

		DEBUG(Worker, "received task: task_id = %d, left = %lf, right = %lf", task.task_id, task.left, task.right);
        task.function = my_func;

        answer_t ans;
        complete_task(&task, &ans);

        if (send(worker->discovery_socket, &ans, sizeof(ans), 0) < 0)
        {
			ERROR(Worker, "send failed: %s", strerror(errno));
            return -1;
        }
		DEBUG(Worker, "sent task result: %lf", ans.result);
    }

    close(worker->discovery_socket);

    return shutdown_worker(worker);
}

int shutdown_worker(worker_t *worker)
{
    free(worker);
    return 0;
}