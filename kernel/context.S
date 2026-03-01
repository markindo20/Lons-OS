.section .text
.intel_syntax noprefix

.global context_switch
.type context_switch, @function

context_switch:
    # Save current task's registers
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx

    # Save current stack pointer (RSP) into prev->rsp
    # RDI is the 1st argument: task_t *prev
    mov [rdi], rsp

    # Load next task's stack pointer (RSP)
    # RSI is the 2nd argument: task_t *next
    mov rsp, [rsi]

    # Restore registers for the new task
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    ret

# Fixes the "missing .note.GNU-stack section" linker warning
.section .note.GNU-stack,"",@progbits