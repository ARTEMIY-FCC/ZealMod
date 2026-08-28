/* на стенде хранилища во флеше нет — заглушки */
#include "../src/nvram.h"
nvram_t nv;
void nv_load(void) { }
int  nv_save(void) { return 1; }
int  nv_valid(void) { return 0; }
void nv_dirty(void) { }
void nv_flush(void) { }
void nv_sync_from_ram(void) { }
void nv_restore_to_ram(void) { }
void nv_boot_io(void) { }
int  nv_pending(void) { return 0; }
void nv_set_pending(int v) { (void)v; }
