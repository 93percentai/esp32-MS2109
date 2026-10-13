#pragma once
#include <stddef.h>
#include <stdint.h>
/* One partial socket write, sharing an absolute deadline across all frame chunks. */
int deadline_send(int socket, const char *data, size_t size, int flags, int64_t deadline_us);
