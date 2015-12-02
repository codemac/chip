typedef struct {
	word_t r4;
	word_t r5;
	word_t r6;
	word_t r7;
	word_t r8;
	word_t r9;
	word_t r10;
	word_t r11;
	word_t r12;
	word_t r13; /* sp */
	word_t r14; /* lr */
} regctx_t;

static inline void setup(regctx_t *ctx, char *stack, void (*retpc)(void)) {
	ctx->r13.ptr = stack - 2*sizeof(uintptr_t);
	ctx->r14.fnptr = retpc;
}
