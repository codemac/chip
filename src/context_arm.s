.global _swapctx
.global __swapctx	
_swapctx:
__swapctx:	
	stm r0, {r5-r14}
	ldm r1, {r5-r14}
	mov pc, lr

.global _loadctx
.global __loadctx
_loadctx:
__loadctx:	
	ldm  r0, {r5-r14}
	mov  pc, lr 
