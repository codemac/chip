struct regctx_s {
	word_t rsp;
	word_t retpc;
};

static inline void setup(regctx_t *ctx, char *stack, void (*retpc)(void)) {
	ctx->rsp.ptr = stack - sizeof(uintptr_t);
	ctx->retpc.fnptr = retpc;
}

static void _swapctx(regctx_t *save, const regctx_t *to) {
	/* 
	 * in order to prevent the compiler from using
	 * our registers after the call, we use them
	 * explicitly and then list them as clobbers.
	 */
	register regctx_t *save_reg __asm__ ("rcx");
	register const regctx_t *to_reg __asm__ ("rdx");
	save_reg = save;
	to_reg = to;
	__asm__ volatile (
		"leaq 1f(%%rip), %%rax \n\t"
		"movq %%rsp, 0(%0)  \n\t"
		"movq %%rax, 8(%0)  \n\t"
		"movq 0(%1), %%rsp  \n\t"
		"jmpq *8(%1)        \n\t"
		"1: \n\t"
		: "+r"(save_reg), "+r"(to_reg)
		: 
		: "%rax", "%rbx", "%rbp", /* "%rcx", "%rdx",*/ "%rdi", "%rsi", "r8",
		  "r9", "r10", "r11", "r12", "r13", "r14", "r15", "cc", "memory"
		);
}

__attribute__((noreturn))
static void _loadctx(const regctx_t *to) {
	__asm__ volatile (
		"movq %0, %%rsp \n\t"
		"jmpq *%1      \n\t"
		: /* no outputs */
		: "r"(to->rsp), "r"(to->retpc)
		: "cc", "memory" /* we shouldn't care about anything else */
		);
	panic("_loadctx returned!");
}
