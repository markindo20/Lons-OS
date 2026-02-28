/*
 * idt.c — IDT + Exception + IRQ handlers
 */

#include "idt.h"
#include "gdt.h"
#include "framebuffer.h"
#include "keyboard.h"

static idt_entry_t idt[256];
static idtr_t      idtr;

/* ── Exception names ── */
static const char *exception_names[32] = {
    "Division By Zero", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 FPU Error", "Alignment Check",
    "Machine Check", "SIMD Floating-Point", "Virtualization",
    "Control Protection", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor Injection", "VMM Communication",
    "Security Exception", "Reserved"
};

/* ── Exception handler (vectors 0-31) ── */
void exception_handler(interrupt_frame_t *frame) {
    fb_console_set_color(FB_BLACK, FB_RED);
    fb_console_print("\n !! KERNEL EXCEPTION !! \n");
    fb_console_set_color(FB_WHITE, FB_BLACK);
    fb_console_print(" Exception : ");
    if (frame->vector < 32) fb_console_print(exception_names[frame->vector]);
    fb_console_print("\n Vector    : "); fb_console_print_hex(frame->vector);
    fb_console_print("\n Error Code: "); fb_console_print_hex(frame->error_code);
    fb_console_print("\n RIP       : "); fb_console_print_hex(frame->rip);
    fb_console_print("\n RSP       : "); fb_console_print_hex(frame->rsp);
    fb_console_print("\n RAX       : "); fb_console_print_hex(frame->rax);
    fb_console_print("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* ── IRQ handler (vectors 32-47) ── */
void irq_handler(interrupt_frame_t *frame) {
    uint8_t irq = (uint8_t)(frame->vector - 32);
    switch (irq) {
        case 1: keyboard_irq_handler(); break;
        /* IRQ0 (timer), IRQ12 (mouse) etc. will be added here later */
        default: break;
    }
    /* pic_send_eoi is called inside each specific handler */
    (void)irq;
}

/* ─────────────────────────────────────────────
 * Exception stubs (vectors 0-31)
 * ───────────────────────────────────────────── */
#define STUB_NOERR(n) \
    __asm__(".global isr_stub_" #n "\nisr_stub_" #n ":\n" \
            "push $0\npush $" #n "\njmp isr_common\n");
#define STUB_ERR(n) \
    __asm__(".global isr_stub_" #n "\nisr_stub_" #n ":\n" \
            "push $" #n "\njmp isr_common\n");

STUB_NOERR(0)  STUB_NOERR(1)  STUB_NOERR(2)  STUB_NOERR(3)
STUB_NOERR(4)  STUB_NOERR(5)  STUB_NOERR(6)  STUB_NOERR(7)
STUB_ERR(8)    STUB_NOERR(9)  STUB_ERR(10)   STUB_ERR(11)
STUB_ERR(12)   STUB_ERR(13)   STUB_ERR(14)   STUB_NOERR(15)
STUB_NOERR(16) STUB_ERR(17)   STUB_NOERR(18) STUB_NOERR(19)
STUB_NOERR(20) STUB_ERR(21)   STUB_NOERR(22) STUB_NOERR(23)
STUB_NOERR(24) STUB_NOERR(25) STUB_NOERR(26) STUB_NOERR(27)
STUB_NOERR(28) STUB_ERR(29)   STUB_ERR(30)   STUB_NOERR(31)

/* ─────────────────────────────────────────────
 * IRQ stubs (vectors 32-47)
 * All hardware IRQs — no error code, just vector number
 * ───────────────────────────────────────────── */
#define IRQ_STUB(n) \
    __asm__(".global irq_stub_" #n "\nirq_stub_" #n ":\n" \
            "push $0\npush $" #n "\njmp irq_common\n");

IRQ_STUB(32) IRQ_STUB(33) IRQ_STUB(34) IRQ_STUB(35)
IRQ_STUB(36) IRQ_STUB(37) IRQ_STUB(38) IRQ_STUB(39)
IRQ_STUB(40) IRQ_STUB(41) IRQ_STUB(42) IRQ_STUB(43)
IRQ_STUB(44) IRQ_STUB(45) IRQ_STUB(46) IRQ_STUB(47)

/* ── Shared exception entry point ── */
__asm__ (
    ".global isr_common\nisr_common:\n"
    "push %rax\npush %rbx\npush %rcx\npush %rdx\n"
    "push %rsi\npush %rdi\npush %rbp\n"
    "push %r8\npush %r9\npush %r10\npush %r11\n"
    "push %r12\npush %r13\npush %r14\npush %r15\n"
    "mov %rsp, %rdi\ncall exception_handler\n"
    "pop %r15\npop %r14\npop %r13\npop %r12\n"
    "pop %r11\npop %r10\npop %r9\npop %r8\n"
    "pop %rbp\npop %rdi\npop %rsi\n"
    "pop %rdx\npop %rcx\npop %rbx\npop %rax\n"
    "add $16, %rsp\niretq\n"
);

/* ── Shared IRQ entry point ── */
__asm__ (
    ".global irq_common\nirq_common:\n"
    "push %rax\npush %rbx\npush %rcx\npush %rdx\n"
    "push %rsi\npush %rdi\npush %rbp\n"
    "push %r8\npush %r9\npush %r10\npush %r11\n"
    "push %r12\npush %r13\npush %r14\npush %r15\n"
    "mov %rsp, %rdi\ncall irq_handler\n"
    "pop %r15\npop %r14\npop %r13\npop %r12\n"
    "pop %r11\npop %r10\npop %r9\npop %r8\n"
    "pop %rbp\npop %rdi\npop %rsi\n"
    "pop %rdx\npop %rcx\npop %rbx\npop %rax\n"
    "add $16, %rsp\niretq\n"
);

/* ── Gate builder ── */
static void idt_set_gate(uint8_t vector, void *handler) {
    uint64_t addr = (uint64_t)handler;
    idt[vector].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = 0x8E;
    idt[vector].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].reserved    = 0;
}

/* Forward declarations — exceptions */
extern void isr_stub_0(void);  extern void isr_stub_1(void);
extern void isr_stub_2(void);  extern void isr_stub_3(void);
extern void isr_stub_4(void);  extern void isr_stub_5(void);
extern void isr_stub_6(void);  extern void isr_stub_7(void);
extern void isr_stub_8(void);  extern void isr_stub_9(void);
extern void isr_stub_10(void); extern void isr_stub_11(void);
extern void isr_stub_12(void); extern void isr_stub_13(void);
extern void isr_stub_14(void); extern void isr_stub_15(void);
extern void isr_stub_16(void); extern void isr_stub_17(void);
extern void isr_stub_18(void); extern void isr_stub_19(void);
extern void isr_stub_20(void); extern void isr_stub_21(void);
extern void isr_stub_22(void); extern void isr_stub_23(void);
extern void isr_stub_24(void); extern void isr_stub_25(void);
extern void isr_stub_26(void); extern void isr_stub_27(void);
extern void isr_stub_28(void); extern void isr_stub_29(void);
extern void isr_stub_30(void); extern void isr_stub_31(void);

/* Forward declarations — IRQs */
extern void irq_stub_32(void); extern void irq_stub_33(void);
extern void irq_stub_34(void); extern void irq_stub_35(void);
extern void irq_stub_36(void); extern void irq_stub_37(void);
extern void irq_stub_38(void); extern void irq_stub_39(void);
extern void irq_stub_40(void); extern void irq_stub_41(void);
extern void irq_stub_42(void); extern void irq_stub_43(void);
extern void irq_stub_44(void); extern void irq_stub_45(void);
extern void irq_stub_46(void); extern void irq_stub_47(void);

void idt_init(void) {
    /* Exceptions 0-31 */
    idt_set_gate(0,  isr_stub_0);  idt_set_gate(1,  isr_stub_1);
    idt_set_gate(2,  isr_stub_2);  idt_set_gate(3,  isr_stub_3);
    idt_set_gate(4,  isr_stub_4);  idt_set_gate(5,  isr_stub_5);
    idt_set_gate(6,  isr_stub_6);  idt_set_gate(7,  isr_stub_7);
    idt_set_gate(8,  isr_stub_8);  idt_set_gate(9,  isr_stub_9);
    idt_set_gate(10, isr_stub_10); idt_set_gate(11, isr_stub_11);
    idt_set_gate(12, isr_stub_12); idt_set_gate(13, isr_stub_13);
    idt_set_gate(14, isr_stub_14); idt_set_gate(15, isr_stub_15);
    idt_set_gate(16, isr_stub_16); idt_set_gate(17, isr_stub_17);
    idt_set_gate(18, isr_stub_18); idt_set_gate(19, isr_stub_19);
    idt_set_gate(20, isr_stub_20); idt_set_gate(21, isr_stub_21);
    idt_set_gate(22, isr_stub_22); idt_set_gate(23, isr_stub_23);
    idt_set_gate(24, isr_stub_24); idt_set_gate(25, isr_stub_25);
    idt_set_gate(26, isr_stub_26); idt_set_gate(27, isr_stub_27);
    idt_set_gate(28, isr_stub_28); idt_set_gate(29, isr_stub_29);
    idt_set_gate(30, isr_stub_30); idt_set_gate(31, isr_stub_31);

    /* Hardware IRQs 32-47 */
    idt_set_gate(32, irq_stub_32); idt_set_gate(33, irq_stub_33);
    idt_set_gate(34, irq_stub_34); idt_set_gate(35, irq_stub_35);
    idt_set_gate(36, irq_stub_36); idt_set_gate(37, irq_stub_37);
    idt_set_gate(38, irq_stub_38); idt_set_gate(39, irq_stub_39);
    idt_set_gate(40, irq_stub_40); idt_set_gate(41, irq_stub_41);
    idt_set_gate(42, irq_stub_42); idt_set_gate(43, irq_stub_43);
    idt_set_gate(44, irq_stub_44); idt_set_gate(45, irq_stub_45);
    idt_set_gate(46, irq_stub_46); idt_set_gate(47, irq_stub_47);

    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint64_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
}