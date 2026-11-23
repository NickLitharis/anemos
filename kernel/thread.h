#ifndef ANEMOS_KERNEL_THREAD_H
#define ANEMOS_KERNEL_THREAD_H

#include <stdint.h>

typedef void (*thread_entry_t)(void *);

struct thread_semaphore {
    unsigned count;
};

/* Single CPU only. Initialize once after timer and interrupt setup; nonzero
 * means success. All other calls require successful initialization. Create
 * returns nonzero on success and fails for a null entry or exhausted slots.
 * Create, yield, sleep and wait require thread context with interrupts enabled;
 * they must not be called from an interrupt or while holding a lock. Entries
 * run with interrupts enabled and may return to terminate. Sleep(0) yields.
 * Semaphore initialization may run in either context only before use; its
 * storage must outlive its waiters. Signal is nonblocking and may run in
 * either context. Its count must not exceed UINT_MAX after a signal. */
int thread_init(void);
int thread_create(thread_entry_t entry, void *arg);
void thread_yield(void);
void thread_sleep(uint64_t ticks);
void thread_semaphore_init(struct thread_semaphore *semaphore, unsigned initial);
void thread_semaphore_wait(struct thread_semaphore *semaphore);
void thread_semaphore_signal(struct thread_semaphore *semaphore);

/* Result of a blocked channel wait, decided atomically with interrupts disabled. */
enum thread_wait_result {
    THREAD_WAIT_SIGNALED,
    THREAD_WAIT_TIMEOUT,
    THREAD_WAIT_CANCELLED
};

/* Internal single-CPU wait channel operations, called with interrupts disabled.
 * Block only from thread context; wake and cancel are nonblocking and wake all
 * waiters. Cancel returns nonzero when at least one waiter was cancelled. */
enum thread_wait_result thread_wait_channel_locked(const void *channel);
/* Positive relative ticks; SIGNALED, TIMEOUT or CANCELLED. */
enum thread_wait_result thread_wait_channel_for_locked(const void *channel, uint64_t ticks);
void thread_wake_channel_locked(const void *channel);
int thread_cancel_channel_locked(const void *channel);

/* Called with interrupts disabled by the timer interrupt after PIC EOI. */
void thread_timer_tick(void);
int thread_self_test(void);

#endif
