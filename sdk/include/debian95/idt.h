/* Debian95 — x86-64 interrupt frame + IDT setup. */
#ifndef DEBIAN95_IDT_H
#define DEBIAN95_IDT_H

#include <debian95/types.h>

/* Register state as pushed by arch/x86_64/isr.asm (low address -> high). */
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vector, errcode;
    u64 rip, cs, rflags, rsp, ss;
} InterruptFrame;

void idt_init(void);

#endif /* DEBIAN95_IDT_H */
