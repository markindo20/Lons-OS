/*
 * context_switch.S — Task context switcher
 *
 * void context_switch(task_t *prev, task_t *next);
 *   rdi = prev  (task_t*, rsp is at offset 0)
 *   rsi = next  (task_t*, rsp is at offset 0)
 *
 * Saves callee-saved registers onto the current stack,
 * stores rsp into prev->rsp, loads rsp from next->rsp,
 * then pops callee-saved registers and returns into
 * the new task.
 */

.global context_switch
context_switch:
    /* Save callee-saved registers onto current stack */
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15

    /* Save current stack pointer into prev->rsp (offset 0) */
    mov  %rsp, (%rdi)

    /* Load new stack pointer from next->rsp (offset 0) */
    mov  (%rsi), %rsp

    /* Restore callee-saved registers from new stack */
    pop  %r15
    pop  %r14
    pop  %r13
    pop  %r12
    pop  %rbx
    pop  %rbp

    ret