#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_KQUEUE

#include "sampler_timer.h"
#include <pthread.h>
#include <sys/event.h>
#include <unistd.h>
#include <errno.h>

static int s_kq = -1;
static pthread_t s_thread;
static sampler_timer_notify_fn s_notify_fn = NULL;
static void *s_user_data = NULL;

static void *timer_thread_fn(void *arg)
{
    (void)arg;
    struct kevent event;

    while (1) {
        int ret = kevent(s_kq, NULL, 0, &event, 1, NULL);
        if (ret == -1) {
            if (errno == EINTR) continue;
            break; /* kq closed or error */
        }
        if (ret > 0 && s_notify_fn) {
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

    s_kq = kqueue();
    if (s_kq == -1) return -1;

    struct kevent kev;
    EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_NSECONDS, period_ns, NULL);

    if (kevent(s_kq, &kev, 1, NULL, 0, NULL) == -1) {
        close(s_kq);
        s_kq = -1;
        return -1;
    }

    if (pthread_create(&s_thread, NULL, timer_thread_fn, NULL) != 0) {
        close(s_kq);
        s_kq = -1;
        return -1;
    }

    return 0;
}

void sampler_timer_stop(void)
{
    if (s_kq != -1) {
        close(s_kq);
        pthread_join(s_thread, NULL);
        s_kq = -1;
    }
    s_notify_fn = NULL;
    s_user_data = NULL;
}

#endif /* HAVE_KQUEUE */
