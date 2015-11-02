.global _swapctx
.global __swapctx	
_swapctx:
__swapctx:	
	push {r5-r12,r14}
	str  r13, [r0]
	ldr  r13, [r1]
	pop  {r5-r12,r15} // implicit ret

.global _loadctx
.global __loadctx
_loadctx:
__loadctx:	
	ldr  r13, [r0]
	pop  {r5-r12,r15} // implicit ret
