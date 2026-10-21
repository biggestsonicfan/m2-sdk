/*
 * vaporbench_gsx.c — the geoserial kernel's syscalls for vaporbench's GSX build (M2_GSX).
 *
 * m2-x11's gs_sdk.h declares every syscall, the m2_* ones too, as stubs into the kernel's
 * table; vaporbench.c has the m2-sdk's own m2_* (m2.h). So the two link calls it needs are
 * wrapped here, in a file of their own.
 */
#include "gs_app.h"

int  vb_gs_poll_exit(void)       { return gs_poll_exit(); }
void vb_gs_puts(const char *s)   { gs_puts(s); }
