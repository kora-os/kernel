/* From musl's arch/aarch64/fp_arch.h. KoraOS: guarded, so that the host unit
 * tests on other machines fall back to the portable versions in libm.h. */
#ifdef __aarch64__

#define fp_barrierf fp_barrierf
static inline float fp_barrierf(float x)
{
	__asm__ __volatile__ ("" : "+w"(x));
	return x;
}

#define fp_barrier fp_barrier
static inline double fp_barrier(double x)
{
	__asm__ __volatile__ ("" : "+w"(x));
	return x;
}

#define fp_force_evalf fp_force_evalf
static inline void fp_force_evalf(float x)
{
	__asm__ __volatile__ ("" : "+w"(x));
}

#define fp_force_eval fp_force_eval
static inline void fp_force_eval(double x)
{
	__asm__ __volatile__ ("" : "+w"(x));
}

#endif /* __aarch64__ */
