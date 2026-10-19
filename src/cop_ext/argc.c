#define VM_HOST
#include "cop_ext.c"
int printf(const char *, ...);
uint32_t host_buf[0x8000];
uint32_t vm_in(void){return 0;} void vm_out(uint32_t v){} uint32_t vm_sin(uint32_t a){return 0;} uint32_t vm_cos(uint32_t a){return 0;}
uint32_t vm_ird(uint32_t a){return 0;} void vm_iwr(uint32_t a, uint32_t v){} void vm_push(void){} void vm_pop(void){} uint32_t vm_rsqrt(uint32_t a){return 0;}
int main(void){ for (uint32_t n = 0; n < 0x88; n++) { uint32_t c = n << 23 | n << 8 | n; int e = ext_args(c); printf("%02x %d\n", n, e != -2 ? e : sharc_args_for_cmd(c)); } }
