/* nvram.h — хранилище, переживающее выключение (сектор флеша). */
#ifndef NVRAM_H
#define NVRAM_H
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint32_t epoch_sec;      /* время суток на момент записи */
    uint32_t hi[64];         /* рекорды: по два слота на модуль */
    uint8_t  cfg[4];         /* настройки */
    uint8_t  btn[4];         /* раскладка кнопок */
    uint32_t stamp;          /* метка настроек из образа */
    uint32_t saves;          /* сколько раз записывали — видно износ */
    uint32_t sum;
} nvram_t;

extern nvram_t nv;
void nv_load(void);
int  nv_save(void);
int  nv_valid(void);
void nv_dirty(void);       /* пометить, что есть что сохранять */
void nv_flush(void);       /* записать, если помечено (и не слишком часто) */
void nv_sync_from_ram(void);
void nv_restore_to_ram(void);
void nv_boot_io(void);
int  nv_pending(void);
void nv_set_pending(int v);
#endif
