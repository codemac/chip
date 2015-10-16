.globl __savectx
__savectx:
	movl    $1, %esi
	xorl    %eax, %eax
	movq    %rsi, 0(%rdi)  /* store ret as 1 */
	movq    %rax, 8(%rdi)  /* clear arg0 (why not) */
	movq	%rbx, 16(%rdi) /* callee-save registers in System V */
	movq	%rbp, 24(%rdi) 
	movq	%r10, 32(%rdi)
	movq	%r11, 40(%rdi)
	movq	%r12, 48(%rdi)
	movq	%r13, 56(%rdi)
	movq	%r14, 64(%rdi)
	movq	%r15, 72(%rdi)
	movq	(%rsp), %rcx	/* %rip */
	leaq	8(%rsp), %rsi	/* %rsp */
	movq	%rcx, 80(%rdi)
	movq	%rsi, 88(%rdi)
	ret     /* return 0 (see top) */

.globl __loadctx
__loadctx:
	movq    0(%rdi), %rax 	/* load return into rax */
	movq	16(%rdi), %rbx
	movq	24(%rdi), %rbp
	movq	32(%rdi), %r10
	movq	40(%rdi), %r11
	movq	48(%rdi), %r12
	movq	56(%rdi), %r13
	movq	64(%rdi), %r14
	movq	72(%rdi), %r15
	movq	80(%rdi), %rsp
	pushq	88(%rdi)	
	movq    8(%rdi), %rdi   /* load arg0 (may be a new ctx) */
	ret     /* return into a different place */
