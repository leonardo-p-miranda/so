// dccthread.c

#include "dccthread.h"
#include "dlist.h"
#include <ucontext.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <time.h>

#define MAX_STACK_SIZE 8192

struct dccthread
{
    const char *name;
    ucontext_t context;
    char stack[MAX_STACK_SIZE];
    dccthread_t *waiting_thread;
    int is_completed;
};

static dccthread_t scheduler;
static dccthread_t *current_thread;
static sigset_t signal_set_rt;
static int sleeping_count;
static int should_exit;

static struct dlist *ready_threads;

dccthread_t *dccthread_self(void)
{
    return current_thread;
}

const char *dccthread_name(dccthread_t *thread)
{
    return thread->name;
}

static void enqueue_ready_thread(dccthread_t *thread)
{
    dlist_push_right(ready_threads, (void *)thread);
}

static dccthread_t *dequeue_ready_thread(void)
{
    assert(!dlist_empty(ready_threads));
    dccthread_t *thread = (dccthread_t *)dlist_get_index(ready_threads, 0);
    dlist_pop_left(ready_threads);
    return thread;
}

static void switch_to_thread(dccthread_t *thread)
{
    dccthread_t *current = dccthread_self();
    current_thread = thread;
    swapcontext(&current->context, &thread->context);
}

void dccthread_yield(void)
{
    sigprocmask(SIG_BLOCK, &signal_set_rt, NULL);
    dccthread_t *current = dccthread_self();
    enqueue_ready_thread(current);
    should_exit = 1;
    switch_to_thread(&scheduler);
    sigprocmask(SIG_UNBLOCK, &signal_set_rt, NULL);
}

static void schedule()
{
    sigprocmask(SIG_BLOCK, &signal_set_rt, NULL);
    while (!dlist_empty(ready_threads) || sleeping_count != 0 || !should_exit)
    {
        if (!dlist_empty(ready_threads))
            switch_to_thread(dequeue_ready_thread());
    }
    sigprocmask(SIG_UNBLOCK, &signal_set_rt, NULL);
}

static void dccthread_sighandler(int signum)
{
    assert(signum == SIGRTMIN);
    should_exit = 1;
    dccthread_yield();
}

static void dccthread_sighandler_sleep(int signum, siginfo_t *info, void *context)
{
    assert(signum == SIGRTMAX);
    sigprocmask(SIG_BLOCK, &signal_set_rt, NULL);
    enqueue_ready_thread(info->si_value.sival_ptr);
    should_exit = 1;
    sigprocmask(SIG_UNBLOCK, &signal_set_rt, NULL);
}

void dccthread_init(void (*func)(int), int param)
{
    ready_threads = dlist_create();

    scheduler.name = "scheduler";
    scheduler.is_completed = 0;
    scheduler.waiting_thread = NULL;
    getcontext(&scheduler.context);
    scheduler.context.uc_link = NULL;
    scheduler.context.uc_stack.ss_sp = scheduler.stack;
    scheduler.context.uc_stack.ss_size = sizeof scheduler.stack;
    makecontext(&scheduler.context, schedule, 0);

    dccthread_create("main", func, param);

    struct sigevent sev;
    timer_t timerid;
    struct itimerspec spec;

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = NULL;
    assert(timer_create(CLOCK_PROCESS_CPUTIME_ID, &sev, &timerid) != -1);

    spec.it_value.tv_sec = 0;
    spec.it_value.tv_nsec = 10000000;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 10000000;

    struct sigaction action, action_sleep;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    action.sa_handler = (void (*)(int))dccthread_yield;

    assert(sigaction(SIGRTMIN, &action, NULL) != -1);

    sigemptyset(&signal_set_rt);
    assert(sigaddset(&signal_set_rt, SIGRTMIN) != -1);
    assert(sigaddset(&signal_set_rt, SIGRTMAX) != -1);

    action_sleep.sa_flags = SA_SIGINFO;
    action_sleep.sa_sigaction = dccthread_sighandler_sleep;
    sigemptyset(&action_sleep.sa_mask);

    assert(sigaction(SIGRTMAX, &action_sleep, NULL) != -1);

    assert(timer_settime(timerid, 0, &spec, NULL) != -1);

    should_exit = 0; // Initialize the exit flag

    // Ensure that the main thread starts executing immediately
    current_thread = dequeue_ready_thread();
    swapcontext(&scheduler.context, &current_thread->context);

    // The rest of your existing code
    while (!dlist_empty(ready_threads) || sleeping_count != 0 || !should_exit)
    {
        current_thread = dequeue_ready_thread();
        if (current_thread != NULL)
            swapcontext(&scheduler.context, &current_thread->context);
    }
}

dccthread_t *dccthread_create(const char *name, void (*func)(int), int param)
{
    dccthread_t *thread = (dccthread_t *)malloc(sizeof(dccthread_t));
    thread->name = name;
    thread->waiting_thread = NULL;
    thread->is_completed = 0;

    getcontext(&thread->context);
    thread->context.uc_link = NULL;
    thread->context.uc_stack.ss_sp = thread->stack;
    thread->context.uc_stack.ss_size = sizeof thread->stack;

    makecontext(&thread->context, (void (*)(void))func, 1, param);

    enqueue_ready_thread(thread);

    return thread;
}

void dccthread_exit(void)
{
    sigprocmask(SIG_BLOCK, &signal_set_rt, NULL);
    dccthread_t *current = dccthread_self();
    current->is_completed = 1;

    if (current->waiting_thread != NULL)
    {
        enqueue_ready_thread(current->waiting_thread);
        current->waiting_thread = NULL;
    }

    if (current != &scheduler)
        switch_to_thread(&scheduler);

    sigprocmask(SIG_UNBLOCK, &signal_set_rt, NULL);
}

void dccthread_wait(dccthread_t *tid)
{
    sigprocmask(SIG_BLOCK, &signal_set_rt, NULL);
    dccthread_t *current = dccthread_self();
    current->waiting_thread = tid;

    if (!tid->is_completed)
    {
        sigprocmask(SIG_UNBLOCK, &signal_set_rt, NULL);
        switch_to_thread(&scheduler);
    }
    else
    {
        sigprocmask(SIG_UNBLOCK, &signal_set_rt, NULL);
    }
}

void dccthread_sleep(struct timespec ts)
{
    sigprocmask(SIG_BLOCK, &signal_set_rt, NULL);
    dccthread_t *current = dccthread_self();

    struct sigevent sev;
    timer_t timerid;
    struct itimerspec spec;

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMAX;
    sev.sigev_value.sival_ptr = current;

    assert(timer_create(CLOCK_REALTIME, &sev, &timerid) != -1);

    spec.it_value = ts;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;

    assert(timer_settime(timerid, 0, &spec, NULL) != -1);

    sleeping_count++;

    // Release the lock before switching to the scheduler
    sigprocmask(SIG_UNBLOCK, &signal_set_rt, NULL);

    should_exit = 0; // Reset the exit flag
    switch_to_thread(&scheduler);

    // Decrement sleeping_count when waking up
    sleeping_count--;
}
