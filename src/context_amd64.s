.globl __swapctx
.globl _swapctx
__swapctx:
_swapctx:	
	push %rbx
	push %rbp
	push %r10
	push %r11
	push %r12
	push %r13
	push %r14
	push %r15
	movq %rsp, (%rdi)
	movq (%rsi), %rsp
	pop  %r15
	pop  %r14
	pop  %r13
	pop  %r12
	pop  %r11
	pop  %r10
	pop  %rbp
	pop  %rbx
	ret

.globl __loadctx
.globl _loadctx
__loadctx:
_loadctx:	
	movq (%rdi), %rsp
	pop  %r15
	pop  %r14
	pop  %r13
	pop  %r12
	pop  %r11
	pop  %r10
	pop  %rbp
	pop  %rbx
	ret
