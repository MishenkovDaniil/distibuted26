#ifndef INTEGRAL_PARSER_H
#define INTEGRAL_PARSER_H

#include <stdlib.h>

#include <base.h>

int parse_worker_args(const int argc, const char **argv, worker_args_t *args);
int parse_master_args(const int argc, const char **argv, master_args_t *args);

#endif /* INTEGRAL_PARSER_H */