#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_ZOMBIE
} task_state_t;

typedef struct task {
    uint64_t rsp;            // The saved stack pointer
    uint64_t kstack_top;     // The top of the kernel stack
    task_state_t state;
    struct task *next;       // Linked list for Round-Robin
} task_t;

void sched_init();
void sched_yield();
void sched_new_task(void (*entry)());

// Prototype for the assembly function (Fixes the warning!)
void context_switch(task_t *prev, task_t *next);

#endif