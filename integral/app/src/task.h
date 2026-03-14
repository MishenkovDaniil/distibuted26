#ifndef INTEGRAL_TASK_HELPER_H
#define INTEGRAL_TASK_HELPER_H

#include <stdlib.h>

#include <base.h>

ssize_t prepare_tasks(task_base_t *main_task, task_t **tasks);
void clear_tasks(task_t **tasks);

static inline void set_task_state(task_t *task, task_state_t state)
{
    task->state = state;
}

static inline task_state_t get_task_state(task_t *task)
{
    return task->state;
}

#endif /* INTEGRAL_TASK_HELPER_H */
