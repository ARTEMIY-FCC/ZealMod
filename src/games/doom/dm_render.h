#ifndef DM_RENDER_H
#define DM_RENDER_H
#include "dm_data.h"

#define VIEW_H 186        /* 3D-вид сверху, ниже — панель состояния */

typedef struct { int x, y, z, ang, sin, cos; } dview;   /* x,y,z — Q8 юниты карты */
extern dview dv;

int  dm_bind(const dgfx *g, const dmap *m);
extern int16_t *dm_fh, *dm_ch;   /* высоты секторов в ОЗУ */
void dm_render(const band *b);
int  dm_render_init(int skyflat, int skytex);
int  dm_point_ssec(int x, int y);
int  dm_sec_of_ssec(int ss);
void dm_sprite(int wx, int wy, int wz, int sprid, int light);
void dm_weapon(int frame, int bob);
#endif
