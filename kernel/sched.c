#include "sched.h"
#include "heap.h"

task_t *current_task = NULL;
task_t *task_list = NULL;

void sched_init() {
    // We treat the current execution (kernel _start) as the first task
    current_task = (task_t*)kmalloc(sizeof(task_t));
    current_task->state = TASK_RUNNING;
    current_task->next = current_task; // Circular list
    task_list = current_task;
}

void sched_yield() {
    task_t *prev = current_task;
    task_t *next = current_task->next;

    // Very simple Round-Robin: just pick the next one in the list
    if (next->state == TASK_READY || next->state == TASK_RUNNING) {
        context_switch(prev, next); // Call the assembly function
    }
}