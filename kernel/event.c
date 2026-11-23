#include "event.h"
#include "thread.h"

#include <stddef.h>

#define EVENT_OBJECT_CAPACITY 8u
#define EVENT_HANDLE_CAPACITY 16u

struct event_object {
    unsigned references;
    int allocated;
    int signaled;
};

struct event_handle_slot {
    uint32_t generation;
    uint32_t access;
    unsigned object_index;
    int active;
};

static struct event_object objects[EVENT_OBJECT_CAPACITY];
static struct event_handle_slot handles[EVENT_HANDLE_CAPACITY];

static uint64_t interrupt_save(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void interrupt_restore(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

static unsigned find_free_object(void)
{
    for (unsigned i = 0u; i < EVENT_OBJECT_CAPACITY; ++i) {
        if (objects[i].allocated == 0) {
            return i;
        }
    }
    return EVENT_OBJECT_CAPACITY;
}

static unsigned find_free_handle(void)
{
    for (unsigned i = 0u; i < EVENT_HANDLE_CAPACITY; ++i) {
        if (handles[i].active == 0) {
            return i;
        }
    }
    return EVENT_HANDLE_CAPACITY;
}

static event_handle_t assign_handle(unsigned index, unsigned object_index, uint32_t access)
{
    struct event_handle_slot *slot = &handles[index];
    ++slot->generation;
    if (slot->generation == 0u) {
        ++slot->generation;
    }
    slot->object_index = object_index;
    slot->access = access;
    slot->active = 1;
    return ((uint64_t)slot->generation << 32u) | (uint64_t)(index + 1u);
}

static enum event_status resolve_handle(
    event_handle_t handle,
    uint32_t required_access,
    struct event_handle_slot **slot_out,
    struct event_object **object_out)
{
    const uint32_t encoded_index = (uint32_t)handle;
    const uint32_t generation = (uint32_t)(handle >> 32u);
    if (encoded_index == 0u || encoded_index > EVENT_HANDLE_CAPACITY || generation == 0u) {
        return EVENT_STATUS_INVALID_HANDLE;
    }
    struct event_handle_slot *slot = &handles[encoded_index - 1u];
    if (slot->active == 0 || slot->generation != generation ||
        slot->object_index >= EVENT_OBJECT_CAPACITY ||
        objects[slot->object_index].allocated == 0) {
        return EVENT_STATUS_INVALID_HANDLE;
    }
    if ((slot->access & required_access) != required_access) {
        return EVENT_STATUS_ACCESS_DENIED;
    }
    *slot_out = slot;
    *object_out = &objects[slot->object_index];
    return EVENT_STATUS_OK;
}

enum event_status event_create(int signaled, uint32_t access, event_handle_t *handle)
{
    if ((signaled != 0 && signaled != 1) || access == 0u ||
        (access & ~(uint32_t)EVENT_ACCESS_ALL) != 0u || handle == NULL) {
        return EVENT_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t flags = interrupt_save();
    const unsigned object_index = find_free_object();
    const unsigned handle_index = find_free_handle();
    if (object_index == EVENT_OBJECT_CAPACITY || handle_index == EVENT_HANDLE_CAPACITY) {
        interrupt_restore(flags);
        return EVENT_STATUS_INSUFFICIENT_RESOURCES;
    }
    objects[object_index].allocated = 1;
    objects[object_index].signaled = signaled;
    objects[object_index].references = 1u;
    *handle = assign_handle(handle_index, object_index, access);
    interrupt_restore(flags);
    return EVENT_STATUS_OK;
}

enum event_status event_duplicate(event_handle_t source, uint32_t access, event_handle_t *handle)
{
    if (handle == NULL || access == 0u ||
        (access & ~(uint32_t)EVENT_ACCESS_ALL) != 0u) {
        return EVENT_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t flags = interrupt_save();
    struct event_handle_slot *source_slot;
    struct event_object *object;
    enum event_status status = resolve_handle(source, 0u, &source_slot, &object);
    if (status == EVENT_STATUS_OK && (access & source_slot->access) != access) {
        status = EVENT_STATUS_ACCESS_DENIED;
    }
    const unsigned handle_index = find_free_handle();
    if (status == EVENT_STATUS_OK && handle_index == EVENT_HANDLE_CAPACITY) {
        status = EVENT_STATUS_INSUFFICIENT_RESOURCES;
    }
    if (status == EVENT_STATUS_OK) {
        ++object->references;
        *handle = assign_handle(handle_index, source_slot->object_index, access);
    }
    interrupt_restore(flags);
    return status;
}

enum event_status event_set(event_handle_t handle)
{
    const uint64_t flags = interrupt_save();
    struct event_handle_slot *slot;
    struct event_object *object;
    const enum event_status status = resolve_handle(handle, EVENT_ACCESS_SET, &slot, &object);
    (void)slot;
    if (status == EVENT_STATUS_OK) {
        object->signaled = 1;
        thread_wake_channel_locked(object);
    }
    interrupt_restore(flags);
    return status;
}

enum event_status event_reset(event_handle_t handle)
{
    const uint64_t flags = interrupt_save();
    struct event_handle_slot *slot;
    struct event_object *object;
    const enum event_status status = resolve_handle(handle, EVENT_ACCESS_RESET, &slot, &object);
    (void)slot;
    if (status == EVENT_STATUS_OK) {
        object->signaled = 0;
    }
    interrupt_restore(flags);
    return status;
}

enum event_status event_query(event_handle_t handle, int *signaled)
{
    if (signaled == NULL) {
        return EVENT_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t flags = interrupt_save();
    struct event_handle_slot *slot;
    struct event_object *object;
    const enum event_status status = resolve_handle(handle, EVENT_ACCESS_QUERY, &slot, &object);
    (void)slot;
    if (status == EVENT_STATUS_OK) {
        *signaled = object->signaled;
    }
    interrupt_restore(flags);
    return status;
}

enum event_status event_wait(event_handle_t handle)
{
    const uint64_t flags = interrupt_save();
    struct event_handle_slot *slot;
    struct event_object *object;
    enum event_status status =
        resolve_handle(handle, EVENT_ACCESS_QUERY, &slot, &object);
    if (status == EVENT_STATUS_OK && object->signaled == 0) {
        ++object->references;
        const enum thread_wait_result result = thread_wait_channel_locked(object);
        --object->references;
        if (result == THREAD_WAIT_CANCELLED) {
            status = EVENT_STATUS_CANCELLED;
        }
        if (object->references == 0u) {
            object->allocated = 0;
            object->signaled = 0;
        }
    }
    interrupt_restore(flags);
    return status;
}

enum event_status event_wait_for(event_handle_t handle, uint64_t ticks)
{
    const uint64_t flags = interrupt_save();
    struct event_handle_slot *slot;
    struct event_object *object;
    enum event_status status =
        resolve_handle(handle, EVENT_ACCESS_QUERY, &slot, &object);
    if (status == EVENT_STATUS_OK && object->signaled == 0) {
        if (ticks == 0u) {
            status = EVENT_STATUS_TIMEOUT;
        } else {
            ++object->references;
            const enum thread_wait_result result =
                thread_wait_channel_for_locked(object, ticks);
            --object->references;
            if (result == THREAD_WAIT_CANCELLED) {
                status = EVENT_STATUS_CANCELLED;
            } else if (result == THREAD_WAIT_TIMEOUT) {
                status = EVENT_STATUS_TIMEOUT;
            }
            if (object->references == 0u) {
                object->allocated = 0;
                object->signaled = 0;
            }
        }
    }
    interrupt_restore(flags);
    return status;
}

enum event_status event_cancel_wait(event_handle_t handle)
{
    const uint64_t flags = interrupt_save();
    struct event_handle_slot *slot;
    struct event_object *object;
    enum event_status status =
        resolve_handle(handle, EVENT_ACCESS_QUERY, &slot, &object);
    (void)slot;
    if (status == EVENT_STATUS_OK) {
        status = thread_cancel_channel_locked(object) != 0
            ? EVENT_STATUS_OK
            : EVENT_STATUS_NOT_WAITING;
    }
    interrupt_restore(flags);
    return status;
}

enum event_status event_close(event_handle_t handle)
{
    const uint64_t flags = interrupt_save();
    struct event_handle_slot *slot;
    struct event_object *object;
    const enum event_status status = resolve_handle(handle, 0u, &slot, &object);
    if (status == EVENT_STATUS_OK) {
        slot->active = 0;
        slot->access = 0u;
        --object->references;
        if (object->references == 0u) {
            object->allocated = 0;
            object->signaled = 0;
        }
    }
    interrupt_restore(flags);
    return status;
}
