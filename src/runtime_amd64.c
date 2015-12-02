typedef struct {
	word_t rbx;
	word_t rbp;
	word_t r10;
	word_t r11;
	word_t r12;
	word_t r13;
	word_t r14;
	word_t r15;
	word_t rsp;
} regctx_t;

static inline void setup(regctx_t *ctx, char *stack, void (*retpc)(void)) {
	ctx->rsp.ptr = stack - 2*sizeof(uintptr_t);
	/* push return value onto the stack */
	word_t retval = { .fnptr = retpc };
	*(word_t *)(ctx->rsp.ptr) = retval;
}
