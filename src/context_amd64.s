.globl __savectx
__savectx:
	xorl    %eax, %eax
	movq	%rbx, 0(%rdi)
	movq	%rbp, 8(%rdi) 
	movq	%r10, 16(%rdi)
	movq	%r11, 24(%rdi)
	movq	%r12, 32(%rdi)
	movq	%r13, 40(%rdi)
	movq	%r14, 48(%rdi)
	movq	%r15, 56(%rdi)
	movq	(%rsp), %rcx	
	leaq	8(%rsp), %rsi	
	movq	%rcx, 64(%rdi)
	movq	%rsi, 72(%rdi)
	ret     /* return 0 (see top) */

.globl __loadctx
__loadctx:
	movl    $1, %eax
	movq	0(%rdi), %rbx
	movq	8(%rdi), %rbp
	movq	16(%rdi), %r10
	movq	24(%rdi), %r11
	movq	32(%rdi), %r12
	movq	40(%rdi), %r13
	movq	48(%rdi), %r14
	movq	56(%rdi), %r15
	movq	72(%rdi), %rsp
	pushq	64(%rdi)	
	ret     /* return _savectx */
