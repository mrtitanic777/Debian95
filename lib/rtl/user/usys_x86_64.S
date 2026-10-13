/* Debian95 — x86-64 user syscall trampoline. SysV-C args arrive in
 * rdi,rsi,rdx,rcx,r8,r9; the kernel ABI wants rax=num, rdi,rsi,rdx,r10,r8.
 * SYSCALL clobbers rcx and r11. */
.intel_syntax noprefix
.text
/* crt0: the kernel passes the entry argument in rdi. It is already in the SysV
 * first-arg register, so just call the C entry (which u_exit()s umain's result). */
.global _start
_start:
    call __start_c
1:  jmp 1b

.global __syscall
__syscall:
    mov rax, rdi        /* num */
    mov rdi, rsi        /* a1  */
    mov rsi, rdx        /* a2  */
    mov rdx, rcx        /* a3  */
    mov r10, r8         /* a4  */
    mov r8,  r9         /* a5  */
    syscall
    ret
