.globl __swapctx
.globl _swapctx
__swapctx:
_swapctx:	
	movq	%rbx, 0(%rdi)
	movq	%rbp, 8(%rdi) 
	movq	%r10, 16(%rdi)
	movq	%r11, 24(%rdi)
	movq	%r12, 32(%rdi)
	movq	%r13, 40(%rdi)
	movq	%r14, 48(%rdi)
	movq	%r15, 56(%rdi)
	movq	%rsp, 64(%rdi)
	movq	0(%rsi), %rbx
	movq	8(%rsi), %rbp
	movq	16(%rsi), %r10
	movq	24(%rsi), %r11
	movq	32(%rsi), %r12
	movq	40(%rsi), %r13
	movq	48(%rsi), %r14
	movq	56(%rsi), %r15
	movq	64(%rsi), %rsp
	ret

.globl __loadctx
.globl _loadctx
__loadctx:
_loadctx:	
	movq	0(%rdi), %rbx
	movq	8(%rdi), %rbp
	movq	16(%rdi), %r10
	movq	24(%rdi), %r11
	movq	32(%rdi), %r12
	movq	40(%rdi), %r13
	movq	48(%rdi), %r14
	movq	56(%rdi), %r15
	movq	64(%rdi), %rsp
	ret
