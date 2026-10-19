/* compat layer: m2-hle2's cpres1 model (src/board/sharc*.h, 2662dda2) on the COP VM */
typedef unsigned int uint32_t;
typedef int int32_t;
typedef signed char int8_t;
typedef short int16_t;
typedef unsigned char uint8_t;
typedef unsigned int size_t;
typedef int bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define LOG_WARN(...) ((void)0)

#ifdef VM_HOST
uint32_t vm_in(void); void vm_out(uint32_t v); uint32_t vm_sin(uint32_t a); uint32_t vm_cos(uint32_t a);
uint32_t vm_ird(uint32_t a); void vm_iwr(uint32_t a, uint32_t v); void vm_push(void); void vm_pop(void);
uint32_t vm_rsqrt(uint32_t a);
extern uint32_t host_buf[0x8000];
#define BUF(w) (host_buf[(w) & 0x7fffu])
#else
/* natives */
static inline uint32_t vm_in(void) { uint32_t r; __asm__ volatile(".insn r 0x0b, 0, 0, %0, x0, x0" : "=r"(r)); return r; }
static inline void vm_out(uint32_t v) { __asm__ volatile(".insn r 0x0b, 1, 0, x0, %0, x0" :: "r"(v)); }
static inline uint32_t vm_sin(uint32_t a) { uint32_t r; __asm__ volatile(".insn r 0x0b, 2, 0, %0, %1, x0" : "=r"(r) : "r"(a)); return r; }
static inline uint32_t vm_cos(uint32_t a) { uint32_t r; __asm__ volatile(".insn r 0x0b, 3, 0, %0, %1, x0" : "=r"(r) : "r"(a)); return r; }
static inline uint32_t vm_ird(uint32_t a) { uint32_t r; __asm__ volatile(".insn r 0x0b, 4, 0, %0, %1, x0" : "=r"(r) : "r"(a)); return r; }
static inline void vm_iwr(uint32_t a, uint32_t v) { __asm__ volatile(".insn r 0x0b, 5, 0, x0, %0, %1" :: "r"(a), "r"(v)); }
static inline void vm_push(void) { __asm__ volatile(".insn r 0x0b, 6, 0, x0, x0, x0" ::: "memory"); }
static inline void vm_pop(void) { __asm__ volatile(".insn r 0x0b, 7, 0, x0, x0, x0" ::: "memory"); }
static inline uint32_t vm_rsqrt(uint32_t a) { uint32_t r; __asm__ volatile(".insn r 0x0b, 0, 1, %0, %1, x0" : "=r"(r) : "r"(a)); return r; }

#define BUF(w) (((volatile uint32_t *)0x10000000u)[(w) & 0x7fffu])
#endif
#ifndef VM_HOST
void *memcpy(void *d, const void *s, size_t n) {
    uint32_t *D = d; const uint32_t *S = s;
    for (n >>= 2; n; n--) *D++ = *S++;
    return d;
}
void *memset(void *d, int c, size_t n) {
    uint32_t *D = d;
    for (n >>= 2; n; n--) *D++ = (uint32_t)c;
    return d;
}

#endif
static inline float sharc_bits_to_float(uint32_t u) { union { uint32_t u; float f; } x; x.u = u; return x.f; }
static inline uint32_t sharc_float_to_bits(float f) { union { uint32_t u; float f; } x; x.f = f; return x.u; }
static inline float fabsf(float f) { return __builtin_fabsf(f); }

#define SHARC_STACK_DEPTH 8
typedef struct {
    float rot[3][3];
    float pos[3];
    int32_t ang[3];
    float pm_bone[16][12];
    float shadow_rot[3][3];
    int reply_count, reply_idx;
} sharc_state_t;
static sharc_state_t g_sharc;

/* DM windows */
static uint32_t dmA[0x5B0];             /* 0x30280..0x3082F, unit cache 0x30420..0x3059F inside */
static uint32_t dmB[0x800];             /* 0x31000..0x313FF and 0x31800..0x31BFF: the 3x3 tables */
static uint32_t dmC[0x250];             /* 0x32000..0x3224F */
static uint32_t dmZ[0x80][20];          /* 0x32300 + n*0x20: words 0..6 and 0x14..0x1F */
#define ROT_CACHE ((float (*)[12])&dmA[0x30420u - 0x30280u])

static uint32_t *dm_ptr(uint32_t a) {
    if (a >= 0x1400000u) {               /* buffer RAM; words 0x4000..0x7D7F are the VM's own */
        uint32_t w = (a - 0x1400000u) & 0x7fffu;
        return w - 0x4000u < 0x3D80u ? 0 : (uint32_t *)&BUF(w);
    }
    if (a >= 0x30280u && a < 0x30830u) return &dmA[a - 0x30280u];
    if (a >= 0x31000u && a < 0x31400u) return &dmB[a - 0x31000u];
    if (a >= 0x31800u && a < 0x31C00u) return &dmB[a - 0x31400u];
    if (a >= 0x32000u && a < 0x32250u) return &dmC[a - 0x32000u];
    if (a >= 0x32300u && a < 0x33300u) {
        uint32_t o = a - 0x32300u, n = o >> 5, k = o & 0x1fu;
        if (k < 7u) return &dmZ[n][k];
        if (k >= 0x14u) return &dmZ[n][k - 13u];
    }
    return 0;
}
static __attribute__((noinline)) uint32_t sharc_dm_get(uint32_t a) { uint32_t *p = dm_ptr(a); return p ? *p : 0; }
static __attribute__((noinline)) void sharc_dm_set(uint32_t a, uint32_t v) { uint32_t *p = dm_ptr(a); if (p) *p = v; }
static inline float sharc_dm_getf(uint32_t a)          { return sharc_bits_to_float(sharc_dm_get(a)); }
static inline void  sharc_dm_setf(uint32_t a, float f) { sharc_dm_set(a, sharc_float_to_bits(f)); }

static inline void sharc_push_u(uint32_t v) { vm_out(v); }
static inline void sharc_push_f(float v) { vm_out(sharc_float_to_bits(v)); }

static inline void sharc_sincos(int32_t a, float *s, float *c) {
    *s = sharc_bits_to_float(vm_sin((uint32_t)a));
    *c = sharc_bits_to_float(vm_cos((uint32_t)a));
}
static inline float sharc_angle_to_rad(int32_t fp) {
    int32_t a = (int32_t)((uint32_t)fp << 16) >> 16;
    return (float)a * 9.58737992e-05f;
}

/* The firmware's divide, 1/sqrt and sqrt: the board's own (the kernel's routines) */
static inline float sharc_fw_div_ex(float num, float d, float *last) { if (last) *last = 1.0f; return num / d; }
static inline float sharc_fw_div(float num, float d) { return num / d; }
static inline float sharc_fw_rsqrt(float x) { if (sharc_float_to_bits(x) == 0) return 0.0f; return sharc_bits_to_float(vm_rsqrt(sharc_float_to_bits(x))); }
static inline float sharc_fw_sqrt(float x) { if (sharc_float_to_bits(x) == 0) return x; return __builtin_sqrtf(x); }

static inline int32_t sharc_logb(float x) {
    uint32_t v = sharc_float_to_bits(x);
    if ((v & 0x7FFFFFFFu) == 0x7F800000u) return 0x7F800000;
    if ((v & 0x7FFFFFFFu) == 0)           return (int32_t)0xFF800000u;
    return (int32_t)((v >> 23) & 0xFFu) - 127;
}
static inline float floorf(float x) {
    if (!(x > -2147483648.0f && x < 2147483648.0f)) return x;
    int32_t i = (int32_t)x;
    float f = (float)i;
    return f > x ? f - 1.0f : f;
}
static inline uint32_t sharc_float_to_int32(float v) {
    if (!(v > -2147483649.0f && v < 2147483648.0f)) return 0x80000000u;
    return (uint32_t)(int32_t)v;
}
#ifdef VM_HOST
#define DMEXT ((unsigned char *)host_buf)
#else
#define DMEXT ((unsigned char *)0x10000000u)
#endif
#define DMEXT_SIZE 0x10000u            /* the GEO half of buffer RAM: the rest is the VM's */
static void vm_sync_in(void) {
    float *m = &g_sharc.rot[0][0];
    for (uint32_t i = 0; i < 12u; i++) m[i] = sharc_bits_to_float(vm_ird(0x60u + i));
}
static void vm_sync_out(void) {
    float *m = &g_sharc.rot[0][0];
    for (uint32_t i = 0; i < 12u; i++) vm_iwr(0x60u + i, sharc_float_to_bits(m[i]));
}
#ifndef VM_HOST
/* rv32i has no multiply or divide: the few the C needs, by shifts */
uint32_t __mulsi3(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (; b; b >>= 1, a <<= 1) if (b & 1u) r += a;
    return r;
}
uint32_t __udivsi3(uint32_t n, uint32_t d) {
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = r << 1 | (n >> i & 1u);
        if (r >= d) { r -= d; q |= 1u << i; }
    }
    return q;
}
int32_t __divsi3(int32_t n, int32_t d) {
    uint32_t q = __udivsi3(n < 0 ? 0u - (uint32_t)n : (uint32_t)n, d < 0 ? 0u - (uint32_t)d : (uint32_t)d);
    return (n < 0) != (d < 0) ? (int32_t)(0u - q) : (int32_t)q;
}
#endif
