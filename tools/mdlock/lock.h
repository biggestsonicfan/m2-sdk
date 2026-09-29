/* lock.h — shared between tools/mdlock/mdlock.c (the core under test) and mus_glue.c */
#define LK_MAX 64
typedef struct { unsigned char w, sz, used; unsigned addr, val; } lk_acc_t;
typedef struct { unsigned d[8], a[8], pc, sr, usp, ssp; } lk_regs_t;
extern lk_acc_t lk_log[LK_MAX];
extern int lk_n, lk_bad;
extern char lk_msg[256];
unsigned lk_peek(unsigned a, int sz);          /* side-effect-free read (ROM/RAM), mdlock.c */
void mus_init(void);
void mus_set(const lk_regs_t *r);
void mus_get(lk_regs_t *r);
int  mus_step(void);
void mus_irq(int level);
void mus_dasm(unsigned pc, char *buf);
