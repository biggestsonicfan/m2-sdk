#include "compat.h"
#include "recips.inc"
#include "atan.inc"
#include "rot2.inc"
#include "coli.inc"
#include "zanzou.inc"
#define COP_ARGS_STREAM (-1)
#include "args.inc"
#include "exec.inc"
#include "ext.inc"
/* the VM's entry: the kernel hands it a command word it has no native code for */
void vm_main(uint32_t cmdno) {
    static uint32_t args[32];
    uint32_t cmd = cmdno << 23 | cmdno << 8 | cmdno;
    int n = ext_args(cmd);
    if (n != -2) {
        vm_sync_in();
        for (int k = 0; k < n; k++) args[k] = vm_in();
        ext_exec(cmd, args);
        vm_sync_out();
        return;
    }
    n = sharc_args_for_cmd(cmd);
    vm_sync_in();
    if (n == COP_ARGS_STREAM) {
        sharc_zanzou_begin();
        while (!sharc_zanzou_feed(vm_in())) ;
    } else {
        for (int k = 0; k < n; k++) args[k] = vm_in();
        sharc_exec(cmd, args, n);
    }
    vm_sync_out();
}
