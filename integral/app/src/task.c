#include <stdlib.h>
#include <math.h>

#include <base.h>
#include <task.h>

static const double DELTA = 0.01;

ssize_t prepare_tasks(task_base_t *main_task, task_t **tasks)
{
    if (main_task->right <= main_task->left)
        return -1;

    size_t tasks_cnt = (size_t)ceil((main_task->right - main_task->left) / DELTA);
    if (tasks_cnt == 0)
        return -1;

    *tasks = (task_t *)malloc(sizeof(task_t) * tasks_cnt);
    if (!*tasks)
        return -1;

    double left = main_task->left;
    for (size_t i = 0; i < tasks_cnt; ++i)
    {
        double right = left + DELTA;
        if (right > main_task->right)
            right = main_task->right;

        (*tasks)[i].base.task_id = i;
        (*tasks)[i].base.function = NULL;
        (*tasks)[i].base.left = left;
        (*tasks)[i].base.right = right;
        (*tasks)[i].state = TASK_PENDING;

        left = right;
    }

    return tasks_cnt;
}

void set_task_state(task_t *task, task_state_t state)
{
    task->state = state;
}

task_state_t get_task_state(task_t *task)
{
    return task->state;
}

void clear_tasks(task_t **tasks)
{
    free(*tasks);
    *tasks = NULL;
}