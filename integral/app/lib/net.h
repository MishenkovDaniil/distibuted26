#ifndef INTEGRAL_NET_H
#define INTEGRAL_NET_H

#include <sys/socket.h>
#include <errno.h>

static inline int send_all(int fd, const void *buf, size_t len)
{
    const char *ptr = (const char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        ptr += sent;
        remaining -= sent;
    }

    return 0;
}

static inline int recv_all(int fd, void *buf, size_t len)
{
    char *ptr = (char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t received = recv(fd, ptr, remaining, 0);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (received == 0)
            return -1;
        ptr += received;
        remaining -= received;
    }

    return 0;
}

#endif /* INTEGRAL_NET_H */
