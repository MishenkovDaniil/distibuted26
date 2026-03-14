#include <stdlib.h>
#include <string.h>

#include <log.h>

#include "parser.h"

int parse_worker_args(const int argc, const char **argv, worker_args_t *args)
{
    (void)argc;
    (void)argv;
#ifdef ENV
    char *master_port    = getenv("MASTER_PORT");
    char *discovery_port = getenv("DISCOVERY_PORT");

    if (!discovery_port || !master_port)
    {
        ERROR(Parser, "DISCOVERY_PORT and MASTER_PORT environment variables must be set");
        return -1;
    }

    args->master_port    = atoi(master_port);
    args->discovery_port = atoi(discovery_port);
#else
    (void)args;
#endif

    return 0;
}

int parse_master_args(const int argc, const char **argv, master_args_t *args)
{
    if (argc != 3)
    {
		ERROR(Parser, "left, right borders must be entered via arguments");
        return -1;
    }

    args->task.left = strtod(argv[1], NULL);
    args->task.right = strtod(argv[2], NULL);

#ifdef ENV
    char *master_port    = getenv("MASTER_PORT");
    char *discovery_port = getenv("DISCOVERY_PORT");

    if (!discovery_port || !master_port)
    {
        ERROR(Parser, "DISCOVERY_PORT and MASTER_PORT environment variables must be set");
        return -1;
    }

    args->master_port    = atoi(master_port);
    args->discovery_port = atoi(discovery_port);

    const char *baddr = getenv("BROADCAST_ADDR");
    if (baddr)
        snprintf(args->broadcast_addr, sizeof(args->broadcast_addr), "%s", baddr);
#endif

    return 0;
}