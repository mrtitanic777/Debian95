/* Debian95 — AArch64 user syscall trampoline. AAPCS64-C args arrive in x0..x5;
 * the kernel ABI wants x8=num, x0..x4 = args. Result comes back in x0. */
.text
/* crt0: the kernel passes the entry argument in x0. Forward it to the C entry,
 * which calls umain() and u_exit()s the result (so this never returns). */
.global _start
_start:
    bl   __start_c
1:  b    1b

.global __syscall
__syscall:
    mov  x8, x0         /* num */
    mov  x0, x1         /* a1  */
    mov  x1, x2         /* a2  */
    mov  x2, x3         /* a3  */
    mov  x3, x4         /* a4  */
    mov  x4, x5         /* a5  */
    svc  #0
    ret
