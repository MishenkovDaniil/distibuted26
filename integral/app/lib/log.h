#ifndef INTEGRAL_LOG_H
#define INTEGRAL_LOG_H

#include <stdio.h>
#include <stdarg.h>

static inline void log_vprintf(const char *level,
                               const char *name,
                               const char *fmt,
                               va_list args)
{
    fprintf(stderr, "[%s][%s]", level, name);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

static inline void log_info(const char *name, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vprintf("INFO", name, fmt, args);
    va_end(args);
}

static inline void log_debug(const char *name, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vprintf("DEBUG", name, fmt, args);
    va_end(args);
}

static inline void log_error(const char *name, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vprintf("ERROR", name, fmt, args);
    va_end(args);
}
#ifdef DEBUG
#define INFO(name, fmt, ...) log_info(#name, fmt, ##__VA_ARGS__)
#define ERROR(name, fmt, ...) log_error(#name, fmt, ##__VA_ARGS__)
#define DEBUG(name, fmt, ...) log_debug(#name, fmt, ##__VA_ARGS__)
#else
#define INFO(name, fmt, ...) log_info(#name, fmt, ##__VA_ARGS__)
#define ERROR(name, fmt, ...) log_error(#name, fmt, ##__VA_ARGS__)
#define DEBUG(name, fmt, ...) (void)0
#endif

#endif /* INTEGRAL_LOG_H */
