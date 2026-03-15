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

    for (size_t i = 0; i < tasks_cnt; ++i)
    {
        double left = main_task->left + i * DELTA;
        double right = main_task->left + (i + 1) * DELTA;
        if (right > main_task->right)
            right = main_task->right;

        (*tasks)[i].base.task_id = i;
        (*tasks)[i].base.function = NULL;
        (*tasks)[i].base.left = left;
        (*tasks)[i].base.right = right;
        (*tasks)[i].state = TASK_PENDING;
        (*tasks)[i].worker_fd = -1;
        (*tasks)[i].base.execution_id = 0;
        (*tasks)[i].deadline = (struct timespec){0};
    }

    return tasks_cnt;
}

void clear_tasks(task_t **tasks)
{
    free(*tasks);
    *tasks = NULL;
}
