/* сгенерировано zealmod emu */
#include "plat.h"

void zm_main(void);
extern const uint8_t cover_zguess[];
extern const px cover_zguess_pal[];

static const app_t app_zguess = { "ZealGuessr", cover_zguess, cover_zguess_pal, zm_main };

const app_t *const apps[] = {
    &app_zguess,
};
const int apps_n = (int)(sizeof apps / sizeof *apps);
