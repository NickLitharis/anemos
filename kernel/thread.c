#include "thread.h"

#include <stddef.h>
#include <stdint.h>

#define THREAD_WORKERS 16u
#define THREAD_COUNT (THREAD_WORKERS + 2u)
#define THREAD_IDLE 1u
#define THREAD_STACK_WORDS 2048u

enum thread_state {
    THREAD_FREE,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_SLEEPING,
    THREAD_WAITING,
    THREAD_CHANNEL_WAITING,
    THREAD_CHANNEL_TIMED_WAITING
};

struct thread {
    uintptr_t *stack;
    thread_entry_t entry;
    void *arg;
    struct thread_semaphore *semaphore;
    const void *wait_channel;
    uint64_t remaining;
    int wait_timed_out;
    int wait_cancelled;
    enum thread_state state;
};

static struct thread threads[THREAD_COUNT];
static uintptr_t stacks[THREAD_COUNT][THREAD_STACK_WORDS] __attribute__((aligned(16)));
static unsigned current;
static int initialized;

extern void thread_switch(uintptr_t **old_stack, uintptr_t *new_stack);

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

/* All scheduler state changes and switches occur with interrupts disabled. */
static void schedule(void)
{
    const unsigned previous = current;
    if (threads[previous].state == THREAD_RUNNING) {
        threads[previous].state = THREAD_READY;
    }

    unsigned next = THREAD_IDLE;
    for (unsigned offset = 1u; offset <= THREAD_COUNT; ++offset) {
        const unsigned candidate = (previous + offset) % THREAD_COUNT;
        if (candidate != THREAD_IDLE && threads[candidate].state == THREAD_READY) {
            next = candidate;
            break;
        }
    }
    threads[next].state = THREAD_RUNNING;
    current = next;
    if (next != previous) {
        thread_switch(&threads[previous].stack, threads[next].stack);
    }
}

static __attribute__((noreturn)) void thread_start(void)
{
    thread_entry_t entry = threads[current].entry;
    void *arg = threads[current].arg;
    __asm__ volatile ("sti" : : : "memory");
    entry(arg);

    (void)interrupt_save();
    threads[current].state = THREAD_FREE;
    schedule();
    __builtin_unreachable();
}

static void prepare_thread(unsigned index, thread_entry_t entry, void *arg)
{
    /* switch pops six callee-saved registers and returns into thread_start.
     * The spare word leaves the new function's stack aligned as for a call. */
    uintptr_t *stack = &stacks[index][THREAD_STACK_WORDS - 1u];
    *--stack = (uintptr_t)thread_start;
    for (unsigned i = 0u; i < 6u; ++i) {
        *--stack = 0u;
    }
    threads[index].stack = stack;
    threads[index].entry = entry;
    threads[index].arg = arg;
    threads[index].semaphore = NULL;
    threads[index].remaining = 0u;
    threads[index].wait_cancelled = 0;
    threads[index].state = THREAD_READY;
}

static void idle_thread(void *arg)
{
    (void)arg;
    for (;;) {
        __asm__ volatile ("cli" : : : "memory");
        schedule();
        /* STI's interrupt shadow makes the check-to-halt transition atomic. */
        __asm__ volatile ("sti; hlt" : : : "memory");
    }
}

int thread_init(void)
{
    const uint64_t flags = interrupt_save();
    if (initialized) {
        interrupt_restore(flags);
        return 0;
    }
    current = 0u;
    threads[0].state = THREAD_RUNNING;
    prepare_thread(THREAD_IDLE, idle_thread, NULL);
    initialized = 1;
    interrupt_restore(flags);
    return 1;
}

int thread_create(thread_entry_t entry, void *arg)
{
    if (entry == NULL) {
        return 0;
    }
    const uint64_t flags = interrupt_save();
    if (initialized) {
        for (unsigned i = 2u; i < THREAD_COUNT; ++i) {
            if (threads[i].state == THREAD_FREE) {
                prepare_thread(i, entry, arg);
                interrupt_restore(flags);
                return 1;
            }
        }
    }
    interrupt_restore(flags);
    return 0;
}

void thread_yield(void)
{
    const uint64_t flags = interrupt_save();
    schedule();
    interrupt_restore(flags);
}

void thread_sleep(uint64_t ticks)
{
    if (ticks == 0u) {
        thread_yield();
        return;
    }
    const uint64_t flags = interrupt_save();
    threads[current].remaining = ticks;
    threads[current].state = THREAD_SLEEPING;
    schedule();
    interrupt_restore(flags);
}

void thread_semaphore_init(struct thread_semaphore *semaphore, unsigned initial)
{
    const uint64_t flags = interrupt_save();
    semaphore->count = initial;
    interrupt_restore(flags);
}

void thread_semaphore_wait(struct thread_semaphore *semaphore)
{
    const uint64_t flags = interrupt_save();
    if (semaphore->count != 0u) {
        --semaphore->count;
    } else {
        threads[current].semaphore = semaphore;
        threads[current].state = THREAD_WAITING;
        schedule();
    }
    interrupt_restore(flags);
}

void thread_semaphore_signal(struct thread_semaphore *semaphore)
{
    const uint64_t flags = interrupt_save();
    for (unsigned i = 0u; i < THREAD_COUNT; ++i) {
        if (threads[i].state == THREAD_WAITING && threads[i].semaphore == semaphore) {
            threads[i].semaphore = NULL;
            threads[i].state = THREAD_READY;
            interrupt_restore(flags);
            return;
        }
    }
    ++semaphore->count;
    interrupt_restore(flags);
}

enum thread_wait_result thread_wait_channel_locked(const void *channel)
{
    threads[current].wait_channel = channel;
    threads[current].wait_cancelled = 0;
    threads[current].state = THREAD_CHANNEL_WAITING;
    schedule();
    if (threads[current].wait_cancelled != 0) {
        return THREAD_WAIT_CANCELLED;
    }
    return THREAD_WAIT_SIGNALED;
}

enum thread_wait_result thread_wait_channel_for_locked(const void *channel, uint64_t ticks)
{
    threads[current].wait_channel = channel;
    threads[current].remaining = ticks;
    threads[current].wait_timed_out = 0;
    threads[current].wait_cancelled = 0;
    threads[current].state = THREAD_CHANNEL_TIMED_WAITING;
    schedule();
    if (threads[current].wait_cancelled != 0) {
        return THREAD_WAIT_CANCELLED;
    }
    return threads[current].wait_timed_out == 0
        ? THREAD_WAIT_SIGNALED : THREAD_WAIT_TIMEOUT;
}

void thread_wake_channel_locked(const void *channel)
{
    for (unsigned i = 0u; i < THREAD_COUNT; ++i) {
        if ((threads[i].state == THREAD_CHANNEL_WAITING ||
             threads[i].state == THREAD_CHANNEL_TIMED_WAITING) &&
            threads[i].wait_channel == channel) {
            threads[i].wait_channel = NULL;
            threads[i].state = THREAD_READY;
        }
    }
}

int thread_cancel_channel_locked(const void *channel)
{
    int cancelled = 0;
    for (unsigned i = 0u; i < THREAD_COUNT; ++i) {
        if ((threads[i].state == THREAD_CHANNEL_WAITING ||
             threads[i].state == THREAD_CHANNEL_TIMED_WAITING) &&
            threads[i].wait_channel == channel) {
            threads[i].wait_channel = NULL;
            threads[i].wait_cancelled = 1;
            threads[i].state = THREAD_READY;
            cancelled = 1;
        }
    }
    return cancelled;
}

void thread_timer_tick(void)
{
    if (!initialized) {
        return;
    }
    for (unsigned i = 0u; i < THREAD_COUNT; ++i) {
        if (threads[i].state == THREAD_SLEEPING && --threads[i].remaining == 0u) {
            threads[i].state = THREAD_READY;
        }
        else if (threads[i].state == THREAD_CHANNEL_TIMED_WAITING &&
                 --threads[i].remaining == 0u) {
            threads[i].wait_channel = NULL;
            threads[i].wait_timed_out = 1;
            threads[i].state = THREAD_READY;
        }
    }
    schedule();
}
