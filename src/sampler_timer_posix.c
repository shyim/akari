#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SIGEV_THREAD_ID

#include "sampler_timer.h"
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#ifndef SAMPLER_SIGNAL
#define SAMPLER_SIGNAL (SIGRTMIN + 2)
#endif

#ifndef HAVE_GETTID
#include <sys/syscall.h>
static pid_t s_gettid(void) { return (pid_t)syscall(SYS_gettid); }
#else
#include <unistd.h>
static pid_t s_gettid(void) { return gettid(); }
#endif

static timer_t s_timer;
static pthread_t s_thread;
static volatile int s_killed = 0;
static sampler_timer_notify_fn s_notify_fn = NULL;
static void *s_user_data = NULL;

/* Condition to signal that the handler thread is ready */
static pthread_mutex_t s_ready_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_ready_cond = PTHREAD_COND_INITIALIZER;
static volatile int s_ready = 0;
static volatile pid_t s_thread_id = 0;

static void *timer_thread_fn(void *arg)
{
    (void)arg;
    sigset_t set;
    siginfo_t info;

    sigemptyset(&set);
    sigaddset(&set, SAMPLER_SIGNAL);

    /* Report our tid so the timer can target this thread */
    pthread_mutex_lock(&s_ready_mutex);
    s_thread_id = s_gettid();
    s_ready = 1;
    pthread_cond_signal(&s_ready_cond);
    pthread_mutex_unlock(&s_ready_mutex);

    while (!s_killed) {
        int ret = sigwaitinfo(&set, &info);
        if (ret == -1) {
            if (errno == EINTR) continue;
            break;
        }
        if (s_killed) break;

        if (s_notify_fn) {
            /* info.si_overrun contains the number of additional expirations;
             * we notify once per call, the overrun count is informational */
            s_notify_fn(s_user_data);
        }
    }
    return NULL;
}

int sampler_timer_start(uint64_t period_ns, sampler_timer_notify_fn notify_fn, void *user_data)
{
    if (period_ns == 0) period_ns = 10000000; /* 10ms minimum */

    s_notify_fn = notify_fn;
    s_user_data = user_data;
    s_killed = 0;
    s_ready = 0;

    /* Block SAMPLER_SIGNAL in the calling thread so it goes to the handler */
    sigset_t block_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SAMPLER_SIGNAL);
    pthread_sigmask(SIG_BLOCK, &block_set, NULL);

    /* Create handler thread */
    if (pthread_create(&s_thread, NULL, timer_thread_fn, NULL) != 0) {
        return -1;
    }

    /* Wait for handler thread to report its tid */
    pthread_mutex_lock(&s_ready_mutex);
    while (!s_ready) {
        pthread_cond_wait(&s_ready_cond, &s_ready_mutex);
    }
    pthread_mutex_unlock(&s_ready_mutex);

    /* Create POSIX timer targeting the handler thread */
    struct sigevent sev;
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SAMPLER_SIGNAL;
    sev._sigev_un._tid = s_thread_id;

    if (timer_create(CLOCK_MONOTONIC, &sev, &s_timer) == -1) {
        s_killed = 1;
        pthread_kill(s_thread, SAMPLER_SIGNAL);
        pthread_join(s_thread, NULL);
        return -1;
    }

    /* Arm the timer */
    struct itimerspec its;
    its.it_value.tv_sec = (time_t)(period_ns / 1000000000ULL);
    its.it_value.tv_nsec = (long)(period_ns % 1000000000ULL);
    if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0) {
        its.it_value.tv_nsec = 1; /* timer disarms with zero value */
    }
    its.it_interval = its.it_value;

    if (timer_settime(s_timer, 0, &its, NULL) == -1) {
        timer_delete(s_timer);
        s_killed = 1;
        pthread_kill(s_thread, SAMPLER_SIGNAL);
        pthread_join(s_thread, NULL);
        return -1;
    }

    return 0;
}

void sampler_timer_stop(void)
{
    /* Disarm timer */
    struct itimerspec its = {{0, 0}, {0, 0}};
    timer_settime(s_timer, 0, &its, NULL);
    timer_delete(s_timer);

    /* Stop handler thread */
    s_killed = 1;
    pthread_kill(s_thread, SAMPLER_SIGNAL);
    pthread_join(s_thread, NULL);

    s_notify_fn = NULL;
    s_user_data = NULL;
}

#endif /* HAVE_SIGEV_THREAD_ID */
