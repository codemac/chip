struct regctx_s {
	word_t sp;
	word_t ret;
};

static inline void setup(regctx_t *ctx, char *stack, void (*retpc)(void)) {
	ctx->sp.ptr = stack - 2*sizeof(uintptr_t);
	ctx->ret.fnptr = retpc;
}

static void _swapctx(regctx_t *save, const regctx_t *load) {
	register regctx_t *save_reg __asm__ ("r1");
	register const regctx_t *load_reg __asm__ ("r2");
	register word_t jmp_reg __asm__ ("r3");

	save_reg = save;
	load_reg = load;
	jmp_reg = load->ret;
	__asm__ volatile (
		"str r13, [%0]     \n\t"
		"ldr r13, [%1]     \n\t"
		"str r15, [%0, #4] \n\t" /* HACK: pc points two insns ahead! */
		"bx  %2            \n\t"
		: "+r"(save_reg), "+r"(load_reg), "+r"(jmp_reg)
		: 
		: "r0", /* "r1", "r2",  "r3", */ "r4", "r5", "r6", "r7", "r8",
		  "r9", "r10", "r11", "r12", "r14", "cc", "memory"
		);
}

__attribute__((noreturn))
static void _loadctx(const regctx_t *load) {
	__asm__ volatile (
		"ldr r13, %0 \n\t"
		"bx  %1      \n\t"
		: /* no outputs */
		: "m"(load->sp), "r"(load->ret)
		: "cc", "memory"
		);
	panic("_loadctx() returned!");
}
