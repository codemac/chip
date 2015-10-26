.globl _swapctx
_swapctx:
	stm r0, {r5-r14}
	ldm r1, {r5-r14}
	mov pc, lr

.globl _loadctx
_loadctx:
	ldm  r0, {r5-r14}
	mov  pc, lr 
