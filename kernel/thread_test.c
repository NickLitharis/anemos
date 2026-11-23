#include "thread_test.h"

#include "interrupts.h"
#include "serial.h"
#include "thread.h"

#include <stdint.h>

#define TEST_FAIL(message) do { \
    static const char failure[] = message "\r\n"; \
    serial_write(failure, sizeof(failure) - 1u); \
    return 0; \
} while (0)
#define TEST_TIMEOUT 100u

static volatile unsigned stage;
static volatile unsigned failed;
static volatile unsigned release_spinner;
static struct thread_semaphore semaphore;
static struct thread_semaphore slots_release;
static struct thread_semaphore slots_done;

static void slot_worker(void *arg)
{
    (void)arg;
    thread_semaphore_wait(&slots_release);
    thread_semaphore_signal(&slots_done);
}

static int wait_for_stage(unsigned target)
{
    uint64_t start = timer_ticks();
    while (stage < target) {
        if (timer_ticks() - start >= TEST_TIMEOUT) {
            return 0;
        }
        thread_yield();
    }
    return 1;
}

static void cooperative_worker(void *arg)
{
    (void)arg;
    volatile uint64_t stack_value = 0x123456789abcdef0ull;
    stage = 1u;
    thread_yield();
    if (stack_value != 0x123456789abcdef0ull || stage != 2u) {
        failed = 1u;
    }
    stage = 3u;
}

static void spinner(void *arg)
{
    (void)arg;
    uint64_t start = timer_ticks();
    stage = 1u;
    while (!release_spinner && timer_ticks() - start < 20u) {
        __asm__ volatile ("pause");
    }
    if (!release_spinner) {
        failed = 1u;
    }
    stage = 2u;
}

static void semaphore_worker(void *arg)
{
    (void)arg;
    stage = 1u;
    thread_semaphore_wait(&semaphore);
    stage = 2u;
}

static void sleep_worker(void *arg)
{
    (void)arg;
    uint64_t start = timer_ticks();
    stage = 1u;
    thread_sleep(3u);
    if (timer_ticks() - start < 3u) {
        failed = 1u;
    }
    stage = 2u;
}

static void zero_sleep_worker(void *arg)
{
    (void)arg;
    stage = 1u;
    thread_sleep(0u);
    stage = 2u;
}

static void short_worker(void *arg)
{
    (void)arg;
    stage = 1u;
}

static int test_cooperative_and_preemption(void)
{
    stage = 0u;
    failed = 0u;
    if (!thread_create(cooperative_worker, 0) || !wait_for_stage(1u)) {
        TEST_FAIL("AK4: cooperative setup failed");
    }
    if (stage != 1u) {
        TEST_FAIL("AK4: cooperative yield failed");
    }
    stage = 2u;
    if (!wait_for_stage(3u) || failed) {
        TEST_FAIL("AK4: cooperative stack invariant failed");
    }
    thread_yield();

    stage = 0u;
    release_spinner = 0u;
    if (!thread_create(spinner, 0) || !wait_for_stage(1u)) {
        TEST_FAIL("AK4: preemption setup failed");
    }
    if (stage != 1u || failed) {
        TEST_FAIL("AK4: timer preemption failed");
    }
    release_spinner = 1u;
    if (!wait_for_stage(2u) || failed) {
        TEST_FAIL("AK4: preemption completion failed");
    }
    thread_yield();
    return 1;
}

static int test_semaphore_and_sleep(void)
{
    stage = 0u;
    thread_semaphore_init(&semaphore, 0u);
    if (!thread_create(semaphore_worker, 0) || !wait_for_stage(1u)) {
        TEST_FAIL("AK4: semaphore setup failed");
    }
    thread_sleep(2u);
    if (stage != 1u) {
        TEST_FAIL("AK4: semaphore did not block");
    }
    thread_semaphore_signal(&semaphore);
    if (!wait_for_stage(2u)) {
        TEST_FAIL("AK4: semaphore wakeup failed");
    }
    thread_yield();

    stage = 0u;
    if (!thread_create(sleep_worker, 0) || !wait_for_stage(1u) ||
        !wait_for_stage(2u) || failed) {
        TEST_FAIL("AK4: timed sleep failed");
    }
    thread_yield();

    stage = 0u;
    if (!thread_create(zero_sleep_worker, 0) || !wait_for_stage(1u) ||
        stage != 1u) {
        TEST_FAIL("AK4: zero sleep did not yield");
    }
    if (!wait_for_stage(2u)) {
        TEST_FAIL("AK4: zero sleep completion failed");
    }
    thread_yield();

    /* A signal sent before a waiter exists must remain available. */
    stage = 0u;
    thread_semaphore_init(&semaphore, 0u);
    thread_semaphore_signal(&semaphore);
    if (!thread_create(semaphore_worker, 0) || !wait_for_stage(2u)) {
        TEST_FAIL("AK4: semaphore early signal lost");
    }
    thread_yield();
    return 1;
}

static int test_thread_lifecycle(void)
{
    for (unsigned i = 0u; i < 128u; ++i) {
        stage = 0u;
        if (!thread_create(short_worker, 0) || !wait_for_stage(1u)) {
            TEST_FAIL("AK4: thread lifetime failed");
        }
        thread_yield();
    }

    /* Exhaust worker slots, release the workers, then verify reuse. */
    thread_semaphore_init(&slots_release, 0u);
    thread_semaphore_init(&slots_done, 0u);
    unsigned created = 0u;
    while (created < 64u && thread_create(slot_worker, 0)) {
        ++created;
    }
    if (created == 0u || created == 64u || thread_create(slot_worker, 0)) {
        TEST_FAIL("AK4: thread slot exhaustion failed");
    }
    for (unsigned i = 0u; i < created; ++i) {
        thread_semaphore_signal(&slots_release);
    }
    for (unsigned i = 0u; i < created; ++i) {
        thread_semaphore_wait(&slots_done);
    }
    thread_yield();
    stage = 0u;
    if (!thread_create(short_worker, 0) || !wait_for_stage(1u)) {
        TEST_FAIL("AK4: thread slot recovery failed");
    }
    thread_yield();
    return 1;
}

static int test_idle_wakeup(void)
{
    /* With no worker runnable, the idle thread must wake the sleeping caller. */
    uint64_t idle_start = timer_ticks();
    thread_sleep(3u);
    if (timer_ticks() - idle_start < 3u) {
        TEST_FAIL("AK4: idle timer wakeup failed");
    }
    return 1;
}

int thread_self_test(void)
{
    return test_cooperative_and_preemption() &&
           test_semaphore_and_sleep() &&
           test_thread_lifecycle() &&
           test_idle_wakeup();
}
