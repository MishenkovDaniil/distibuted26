#ifndef INTEGRAL_BASE_H
#define INTEGRAL_BASE_H

#include <stdbool.h>
#include <arpa/inet.h>

typedef enum task_state
{
    TASK_PENDING,
    TASK_IN_PROGRESS,
    TASK_COMPLETED
} task_state_t;

typedef double (*function_t)(double);
typedef struct task_base
{
    double left;
    double right;
    function_t function;
    size_t task_id;
} task_base_t;

typedef struct answer
{
    size_t task_id;
    double result;
} answer_t;

typedef struct task
{
    task_base_t base;
    int worker_fd;
    task_state_t state;
} task_t;

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
    task_base_t task;
} master_args_t;

#endif /* INTEGRAL_BASE_H */
