#include "event_test.h"

#include "event.h"
#include "serial.h"
#include "thread.h"
#include "interrupts.h"
#include <stddef.h>

#define TEST_FAIL(message) do { \
    static const char failure[] = message "\r\n"; \
    serial_write(failure, sizeof(failure) - 1u); \
    return 0; \
} while (0)

static int test_state_and_rights(void)
{
    event_handle_t full;
    event_handle_t query_only;
    int signaled = 0;
    if (event_create(1, EVENT_ACCESS_ALL, &full) != EVENT_STATUS_OK ||
        event_duplicate(full, EVENT_ACCESS_QUERY, &query_only) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: event creation failed");
    }
    if (event_query(query_only, &signaled) != EVENT_STATUS_OK || signaled != 1 ||
        event_reset(full) != EVENT_STATUS_OK ||
        event_query(query_only, &signaled) != EVENT_STATUS_OK || signaled != 0 ||
        event_set(full) != EVENT_STATUS_OK ||
        event_query(query_only, &signaled) != EVENT_STATUS_OK || signaled != 1) {
        TEST_FAIL("AK6: event state transition failed");
    }
    if (event_set(query_only) != EVENT_STATUS_ACCESS_DENIED ||
        event_reset(query_only) != EVENT_STATUS_ACCESS_DENIED ||
        event_close(full) != EVENT_STATUS_OK ||
        event_query(full, &signaled) != EVENT_STATUS_INVALID_HANDLE ||
        event_query(query_only, &signaled) != EVENT_STATUS_OK ||
        event_close(query_only) != EVENT_STATUS_OK ||
        event_query(query_only, &signaled) != EVENT_STATUS_INVALID_HANDLE) {
        TEST_FAIL("AK6: event rights or lifetime failed");
    }
    return 1;
}

static int test_pool_limits_and_reuse(void)
{
    event_handle_t handles[8];
    for (unsigned i = 0u; i < 8u; ++i) {
        if (event_create(0, EVENT_ACCESS_ALL, &handles[i]) != EVENT_STATUS_OK) {
            TEST_FAIL("AK6: event object capacity too small");
        }
    }
    event_handle_t extra;
    if (event_create(0, EVENT_ACCESS_ALL, &extra) !=
        EVENT_STATUS_INSUFFICIENT_RESOURCES) {
        TEST_FAIL("AK6: event object exhaustion not enforced");
    }
    for (unsigned i = 0u; i < 8u; ++i) {
        if (event_close(handles[i]) != EVENT_STATUS_OK) {
            TEST_FAIL("AK6: event pool cleanup failed");
        }
    }

    event_handle_t base;
    event_handle_t duplicates[15];
    if (event_create(0, EVENT_ACCESS_ALL, &base) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: event reuse failed");
    }
    for (unsigned i = 0u; i < 15u; ++i) {
        if (event_duplicate(base, EVENT_ACCESS_QUERY, &duplicates[i]) !=
            EVENT_STATUS_OK) {
            TEST_FAIL("AK6: handle capacity too small");
        }
    }
    if (event_duplicate(base, EVENT_ACCESS_QUERY, &extra) !=
        EVENT_STATUS_INSUFFICIENT_RESOURCES) {
        TEST_FAIL("AK6: handle exhaustion not enforced");
    }
    for (unsigned i = 0u; i < 15u; ++i) {
        if (event_close(duplicates[i]) != EVENT_STATUS_OK) {
            TEST_FAIL("AK6: duplicate handle cleanup failed");
        }
    }
    if (event_close(base) != EVENT_STATUS_OK ||
        event_create(0, EVENT_ACCESS_ALL, &extra) != EVENT_STATUS_OK ||
        extra == base || event_close(extra) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: stale handle survived slot reuse");
    }
    return 1;
}

static volatile unsigned wait_started;
static volatile unsigned wait_finished;
static volatile unsigned wait_failed;
static event_handle_t wait_handle;

static void event_wait_worker(void *argument)
{
    (void)argument;
    __atomic_fetch_add(&wait_started, 1u, __ATOMIC_RELAXED);
    if (event_wait(wait_handle) != EVENT_STATUS_OK) {
        wait_failed = 1u;
    }
    __atomic_fetch_add(&wait_finished, 1u, __ATOMIC_RELAXED);
}

static int await_count(volatile unsigned *value, unsigned target)
{
    const uint64_t start = timer_ticks();
    while (*value < target && timer_ticks() - start < 100u) {
        thread_yield();
    }
    return *value >= target;
}

static int test_event_wait(void)
{
    event_handle_t setter;
    event_handle_t denied;
    wait_started = 0u;
    wait_finished = 0u;
    wait_failed = 0u;
    if (event_create(1, EVENT_ACCESS_ALL, &wait_handle) != EVENT_STATUS_OK ||
        event_wait(wait_handle) != EVENT_STATUS_OK ||
        event_reset(wait_handle) != EVENT_STATUS_OK ||
        event_duplicate(wait_handle, EVENT_ACCESS_SET, &setter) != EVENT_STATUS_OK ||
        event_duplicate(wait_handle, EVENT_ACCESS_SET, &denied) != EVENT_STATUS_OK ||
        event_wait(denied) != EVENT_STATUS_ACCESS_DENIED ||
        event_close(denied) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: event wait immediate or access failed");
    }
    if (!thread_create(event_wait_worker, NULL) ||
        !thread_create(event_wait_worker, NULL) ||
        !await_count(&wait_started, 2u)) {
        TEST_FAIL("AK6: event wait worker setup failed");
    }
    thread_sleep(2u);
    if (wait_finished != 0u || wait_failed != 0u ||
        event_close(wait_handle) != EVENT_STATUS_OK ||
        event_set(setter) != EVENT_STATUS_OK ||
        !await_count(&wait_finished, 2u) || wait_failed != 0u ||
        event_close(setter) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: event wait wakeup or lifetime failed");
    }
    return 1;
}

static volatile unsigned timed_started;
static volatile unsigned timed_done;
static volatile enum event_status timed_result;
static event_handle_t timed_handle;

static void timed_wait_worker(void *argument)
{
    (void)argument;
    timed_started = 1u;
    timed_result = event_wait_for(timed_handle, 50u);
    timed_done = 1u;
}

static int test_timed_wait(void)
{
    event_handle_t setter;
    event_handle_t denied;
    if (event_create(0, EVENT_ACCESS_ALL, &timed_handle) != EVENT_STATUS_OK ||
        event_wait_for(timed_handle, 0u) != EVENT_STATUS_TIMEOUT ||
        event_duplicate(timed_handle, EVENT_ACCESS_SET, &denied) != EVENT_STATUS_OK ||
        event_wait_for(denied, 0u) != EVENT_STATUS_ACCESS_DENIED ||
        event_close(denied) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: timed event polling failed");
    }
    const uint64_t start = timer_ticks();
    if (event_wait_for(timed_handle, 3u) != EVENT_STATUS_TIMEOUT ||
        timer_ticks() - start < 3u) {
        TEST_FAIL("AK6: timed event expiration failed");
    }
    if (event_set(timed_handle) != EVENT_STATUS_OK ||
        event_wait_for(timed_handle, 0u) != EVENT_STATUS_OK ||
        event_reset(timed_handle) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: timed event signaled polling failed");
    }
    timed_started = 0u;
    timed_done = 0u;
    if (event_duplicate(timed_handle, EVENT_ACCESS_SET, &setter) != EVENT_STATUS_OK ||
        !thread_create(timed_wait_worker, NULL) ||
        !await_count(&timed_started, 1u)) {
        TEST_FAIL("AK6: timed event setup failed");
    }
    thread_sleep(2u);
    if (timed_done != 0u || event_close(timed_handle) != EVENT_STATUS_OK ||
        event_set(setter) != EVENT_STATUS_OK ||
        !await_count(&timed_done, 1u) || timed_result != EVENT_STATUS_OK ||
        event_wait_for(setter, 0u) != EVENT_STATUS_ACCESS_DENIED ||
        event_close(setter) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: timed event signaling failed");
    }
    return 1;
}

int event_self_test(void)
{
    return test_state_and_rights() && test_pool_limits_and_reuse() &&
           test_event_wait() && test_timed_wait();
}

static volatile unsigned cancel_started;
static volatile unsigned cancel_done;
static volatile enum event_status cancel_result;
static volatile int cancel_timed;
static event_handle_t cancel_handle;

static void cancel_wait_worker(void *argument)
{
    (void)argument;
    __atomic_fetch_add(&cancel_started, 1u, __ATOMIC_RELAXED);
    cancel_result = cancel_timed != 0
        ? event_wait_for(cancel_handle, 1000u)
        : event_wait(cancel_handle);
    __atomic_fetch_add(&cancel_done, 1u, __ATOMIC_RELAXED);
}

int event_cancel_self_test(void)
{
    event_handle_t second;
    event_handle_t denied;
    int signaled = 0;
    cancel_timed = 0;
    cancel_started = 0u;
    cancel_done = 0u;
    cancel_result = EVENT_STATUS_OK;
    if (event_create(0, EVENT_ACCESS_ALL, &cancel_handle) != EVENT_STATUS_OK ||
        event_cancel_wait(0u) != EVENT_STATUS_INVALID_HANDLE ||
        event_cancel_wait(cancel_handle) != EVENT_STATUS_NOT_WAITING ||
        event_duplicate(cancel_handle, EVENT_ACCESS_SET, &denied) != EVENT_STATUS_OK ||
        event_cancel_wait(denied) != EVENT_STATUS_ACCESS_DENIED ||
        event_close(denied) != EVENT_STATUS_OK ||
        event_duplicate(cancel_handle, EVENT_ACCESS_ALL, &second) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: wait cancellation setup failed");
    }
    if (!thread_create(cancel_wait_worker, NULL) ||
        !await_count(&cancel_started, 1u)) {
        TEST_FAIL("AK6: wait cancellation worker setup failed");
    }
    thread_sleep(2u);
    if (cancel_done != 0u ||
        event_cancel_wait(cancel_handle) != EVENT_STATUS_OK ||
        !await_count(&cancel_done, 1u) ||
        cancel_result != EVENT_STATUS_CANCELLED ||
        event_query(cancel_handle, &signaled) != EVENT_STATUS_OK ||
        signaled != 0) {
        TEST_FAIL("AK6: wait cancellation failed");
    }
    if (event_set(cancel_handle) != EVENT_STATUS_OK ||
        event_wait(cancel_handle) != EVENT_STATUS_OK ||
        event_reset(cancel_handle) != EVENT_STATUS_OK) {
        TEST_FAIL("AK6: cancelled wait consumed a signal");
    }
    cancel_timed = 1;
    cancel_started = 0u;
    cancel_done = 0u;
    if (!thread_create(cancel_wait_worker, NULL) ||
        !await_count(&cancel_started, 1u)) {
        TEST_FAIL("AK6: timed cancellation worker setup failed");
    }
    thread_sleep(2u);
    if (cancel_done != 0u ||
        event_cancel_wait(second) != EVENT_STATUS_OK ||
        !await_count(&cancel_done, 1u) ||
        cancel_result != EVENT_STATUS_CANCELLED) {
        TEST_FAIL("AK6: timed wait cancellation failed");
    }
    cancel_timed = 0;
    cancel_started = 0u;
    cancel_done = 0u;
    if (!thread_create(cancel_wait_worker, NULL) ||
        !await_count(&cancel_started, 1u)) {
        TEST_FAIL("AK6: close-while-blocked worker setup failed");
    }
    thread_sleep(2u);
    if (cancel_done != 0u ||
        event_close(cancel_handle) != EVENT_STATUS_OK ||
        event_cancel_wait(second) != EVENT_STATUS_OK ||
        !await_count(&cancel_done, 1u) ||
        cancel_result != EVENT_STATUS_CANCELLED ||
        event_close(second) != EVENT_STATUS_OK ||
        event_query(second, &signaled) != EVENT_STATUS_INVALID_HANDLE) {
        TEST_FAIL("AK6: close-while-blocked cancellation failed");
    }
    return 1;
}
