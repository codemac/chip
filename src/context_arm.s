.globl __savectx
__savectx:
	stmia r0!, {r5-r14}
	mov   r0, #0
	mov   pc, lr

.globl __loadctx
__loadctx:
	ldmia r0!, {r5-r14}
	mov   r0, #1
	mov   pc, lr 
