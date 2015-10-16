.globl __savectx
__savectx:
	mov   r1, #1
	stmia r0!, {r1,r5-r14}  /* save arg0 + callee-save regs */
	mov   r0, #0			/* return 0 */
	mov   pc, lr

.globl __loadctx
__loadctx:
	mov   r1, r0
	ldmia r1!, {r0,r5-r14}	/* load return into r0 + callee-save regs */
	mov   pc, lr 			/* pc has been set to caller of __savectx */
