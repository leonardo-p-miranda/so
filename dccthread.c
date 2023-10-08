#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>
#include "dccthread.h"

#define MAX_THREADS 100

struct dccthread
{
    ucontext_t uc;
    const char *name;
    int id;
};

typedef struct dccthread dccthread_t;

static dccthread_t threads[MAX_THREADS]; // Assuming a fixed number of threads for simplicity
static int total_threads = 0;

void dccthread_init(void (*func)(int), int param)
{
    // Initialize the manager thread
    if (getcontext(&(threads[total_threads].uc)) == -1)
    {
        fprintf(stderr, "Error while getting context. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    threads[total_threads].uc.uc_link = NULL;
    threads[total_threads].uc.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
    threads[total_threads].uc.uc_stack.ss_size = THREAD_STACK_SIZE;

    if (threads[total_threads].uc.uc_stack.ss_sp == NULL)
    {
        fprintf(stderr, "Error: Failed to allocate stack. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    makecontext(&(threads[total_threads].uc), manager_thread, 0);
    threads[total_threads].name = "gerente";
    total_threads++;

    // Initialize the main thread
    if (getcontext(&(threads[total_threads].uc)) == -1)
    {
        fprintf(stderr, "Error while getting context. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    threads[total_threads].uc.uc_link = NULL;
    threads[total_threads].uc.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
    threads[total_threads].uc.uc_stack.ss_size = THREAD_STACK_SIZE;

    if (threads[total_threads].uc.uc_stack.ss_sp == NULL)
    {
        fprintf(stderr, "Error: Failed to allocate stack. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    makecontext(&(threads[total_threads].uc), (void (*)(void))func, 1, param);
    threads[total_threads].name = "principal";
    total_threads++;

    // Execute the main thread
    setcontext(&(threads[total_threads - 1].uc));
}

dccthread_t *dccthread_create(const char *name, void (*func)(int), int param)
{
    if (total_threads >= MAX_THREADS)
    {
        fprintf(stderr, "Error: Maximum number of threads reached.\n");
        return NULL;
    }

    dccthread_t *thread = &threads[total_threads++];
    thread->name = name;

    if (getcontext(&(thread->uc)) == -1)
    {
        fprintf(stderr, "Error while getting context. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    // Modifying context to point to a new stack
    thread->uc.uc_link = NULL; // Set uc_link to NULL for now; you can adjust it based on your scheduling logic
    thread->uc.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
    thread->uc.uc_stack.ss_size = THREAD_STACK_SIZE;

    // Creating the context
    makecontext(&(thread->uc), (void (*)())func, 1, param);

    return thread;
}
