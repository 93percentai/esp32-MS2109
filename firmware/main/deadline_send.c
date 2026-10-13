#include "deadline_send.h"
#include <errno.h>
#include <stdbool.h>
#include "lwip/sockets.h"
#include "esp_timer.h"

int deadline_send(int socket, const char *data, size_t size, int flags, int64_t deadline_us)
{
    if (socket < 0 || socket >= FD_SETSIZE) { errno = EBADF; return -1; }
    if (!size) return 0;
    while (true) {
        int64_t remaining = deadline_us - esp_timer_get_time();
        if (remaining <= 0) { errno = ETIMEDOUT; return -1; }
        int sent = send(socket, data, size, flags | MSG_DONTWAIT);
        if (sent > 0) return sent;
        if (sent == 0) { errno = ECONNRESET; return -1; }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        if (remaining > 20000) remaining = 20000;
        struct timeval timeout = {.tv_sec=0, .tv_usec=remaining};
        fd_set writable;
        FD_ZERO(&writable); FD_SET(socket, &writable);
        int ready = select(socket + 1, NULL, &writable, NULL, &timeout);
        if (ready < 0 && errno != EINTR) return -1;
    }
}
