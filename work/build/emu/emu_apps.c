/* сгенерировано zealmod emu */
#include "plat.h"

void zm_main(void);
extern const uint8_t cover_blobik[];
extern const px cover_blobik_pal[];

static const app_t app_blobik = { "blobik", cover_blobik, cover_blobik_pal, zm_main };

const app_t *const apps[] = {
    &app_blobik,
};
const int apps_n = (int)(sizeof apps / sizeof *apps);
