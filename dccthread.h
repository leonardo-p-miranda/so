// dccthread.h

#ifndef __DCCTHREAD_HEADER__
#define __DCCTHREAD_HEADER__

#include <time.h>

typedef struct dccthread dccthread_t;

#define DCCTHREAD_MAX_NAME_SIZE 256

void dccthread_init(void (*func)(int), int param) __attribute__((noreturn));

dccthread_t *dccthread_create(const char *name, void (*func)(int), int param);

void dccthread_yield(void);

void dccthread_exit(void);

void dccthread_wait(dccthread_t *tid);

dccthread_t *dccthread_self(void);

const char *dccthread_name(dccthread_t *tid);

#endif
