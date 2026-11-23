#ifndef ANEMOS_KERNEL_EVENT_H
#define ANEMOS_KERNEL_EVENT_H

#include <stdint.h>

typedef uint64_t event_handle_t;

enum event_access {
    EVENT_ACCESS_SET = 1u,
    EVENT_ACCESS_RESET = 2u,
    EVENT_ACCESS_QUERY = 4u,
    EVENT_ACCESS_ALL = 7u
};

enum event_status {
    EVENT_STATUS_OK,
    EVENT_STATUS_INVALID_ARGUMENT,
    EVENT_STATUS_INSUFFICIENT_RESOURCES,
    EVENT_STATUS_INVALID_HANDLE,
    EVENT_STATUS_ACCESS_DENIED,
    EVENT_STATUS_TIMEOUT,
    EVENT_STATUS_CANCELLED,
    EVENT_STATUS_NOT_WAITING
};

enum event_status event_create(int signaled, uint32_t access, event_handle_t *handle);
enum event_status event_duplicate(event_handle_t source, uint32_t access, event_handle_t *handle);
enum event_status event_set(event_handle_t handle);
enum event_status event_reset(event_handle_t handle);
enum event_status event_query(event_handle_t handle, int *signaled);
/* Thread context with interrupts enabled; manual-reset events do not consume signals. */
enum event_status event_wait(event_handle_t handle);
enum event_status event_wait_for(event_handle_t handle, uint64_t ticks);

/* Cancel all blocked waits on the event object; waiters return EVENT_STATUS_CANCELLED
 * and the signaled state is unchanged. Query access required; thread context with
 * interrupts enabled. Returns EVENT_STATUS_NOT_WAITING when no thread is blocked. */
enum event_status event_cancel_wait(event_handle_t handle);
enum event_status event_close(event_handle_t handle);

#endif
