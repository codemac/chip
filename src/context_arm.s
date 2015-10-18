.globl _savectx
_savectx:
	stm r0, {r5-r14}
	mov r0, #0
	mov pc, lr

.globl _loadctx
_loadctx:
	ldm  r0, {r5-r14}
	mov  r0, #1
	mov  pc, lr 
