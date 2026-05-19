#ifndef SAMPLER_TIMER_H
#define SAMPLER_TIMER_H

#include <stdint.h>

/**
 * Callback invoked from the timer handler thread when the timer fires.
 * Must be safe to call from a background thread.
 */
typedef void (*sampler_timer_notify_fn)(void *user_data);

/**
 * Initialize and start a periodic sampling timer.
 * @param period_ns   Timer period in nanoseconds
 * @param notify_fn   Called from handler thread on each tick
 * @param user_data   Passed to notify_fn
 * @return 0 on success, -1 on failure
 */
int sampler_timer_start(uint64_t period_ns, sampler_timer_notify_fn notify_fn, void *user_data);

/**
 * Stop and clean up the sampling timer.
 * Blocks until the handler thread has exited.
 */
void sampler_timer_stop(void);

#endif /* SAMPLER_TIMER_H */
