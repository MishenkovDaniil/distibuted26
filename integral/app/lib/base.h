#ifndef INTEGRAL_BASE_H
#define INTEGRAL_BASE_H

#include <stdbool.h>
#include <arpa/inet.h>

typedef double (*function_t)(double);
typedef struct integral_task
{
    double left;
    double right;
    function_t function;
    double result;
    bool is_completed;
} integral_task_t;

typedef struct worker_args
{
    int discovery_port;
    int master_port;
} worker_args_t;

typedef struct master_args
{
    int master_port;
    int discovery_port;
    char broadcast_addr[INET_ADDRSTRLEN];
    integral_task_t task;
} master_args_t;

#endif /* INTEGRAL_BASE_H */
