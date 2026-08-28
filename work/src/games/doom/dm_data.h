/* dm_data.h — как читаются данные, собранные tools/mkdoom.py.
 *
 * Всё лежит во флеше и отображено в память, поэтому структуры читаются прямо
 * оттуда: ни байта не копируется в ОЗУ.  Поля выровнены по 2 — иначе RISC-V
 * ловит исключение на невыровненном доступе. */
#ifndef DM_DATA_H
#define DM_DATA_H
#include "plat.h"
#include "fx.h"

typedef struct { int16_t x, y; } dvert;
typedef struct { uint16_t v1, v2, flags; int16_t special, tag; int16_t side[2]; } dline;
typedef struct { int16_t xoff, yoff; uint16_t top, bot, mid, sector; } dside;
typedef struct { uint16_t v1, v2; int16_t angle; uint16_t line, side; int16_t off; } dseg;
typedef struct { uint16_t count, first; } dssec;
typedef struct { int16_t x, y, dx, dy; int16_t bbox[2][4]; uint16_t child[2]; } dnode;
typedef struct { int16_t floor, ceil; uint16_t fpic, cpic; int16_t light, special, tag; } dsec;
typedef struct { int16_t x, y, angle; uint16_t type, flags; } dthing;

#define ML_BLOCKING     1
#define ML_TWOSIDED     4
#define ML_DONTPEGTOP   8
#define ML_DONTPEGBOTTOM 16
#define NF_SUBSECTOR    0x8000

typedef struct {
    const dvert *vert;  int nvert;
    const dline *line;  int nline;
    const dside *side;  int nside;
    const dseg  *seg;   int nseg;
    const dssec *ssec;  int nssec;
    const dnode *node;  int nnode;
    const dsec  *sec;   int nsec;
    const dthing *thing; int nthing;
} dmap;

typedef struct { uint16_t worig, horig, w, h; uint32_t off; } dtex;
typedef struct { uint16_t w, h; int16_t xoff, yoff; uint32_t off; } dspr;
/* тип вещи: с какого кадра спрайты, сколько кадров и поворотов, чем является.
 * deathbase/ndeath — кадры падения (без поворотов), последний остаётся трупом. */
typedef struct { uint16_t type, sprbase; uint8_t nframes, nrots, flags, hp;
                 int16_t radius, height; uint16_t deathbase; uint8_t ndeath, pad; } dtinfo;
#define TF_PICKUP 1
#define TF_SOLID  2
#define TF_ENEMY  4

typedef struct {
    const uint16_t *pal;        /* 256 цветов, уже в порядке байт панели */
    const uint8_t  *cmap;       /* 32 уровня освещения по 256 байт */
    const dtex     *tex;
    const uint8_t  *texdata;
    int             ntex;
    const uint32_t *flattab;    /* пары (смещение, сторона) */
    const uint8_t  *flatdata;
    int             nflat;
    int             nmaps;
    int             skyflat, skytex;
    const dspr     *spr;
    const uint8_t  *sprdata;
    const dtinfo   *tinfo;
    int             ntinfo;
    const dspr     *wpn;
    const uint8_t  *wpndata;
    int             nwpn;
} dgfx;

int  dm_open(dgfx *g);              /* разобрать блобы */
int  dm_load(const dgfx *g, dmap *m, int idx);

static inline const uint8_t *dm_sprpix(const dgfx *g, int id, const dspr **out)
{
    *out = &g->spr[id];
    return g->sprdata + g->spr[id].off;
}

static inline const dtinfo *dm_tinfo(const dgfx *g, int type)
{
    for (int i = 0; i < g->ntinfo; i++) if (g->tinfo[i].type == type) return &g->tinfo[i];
    return 0;
}

static inline const uint8_t *dm_tex(const dgfx *g, int id, const dtex **out)
{
    if ((unsigned)id >= (unsigned)g->ntex) id = 0;
    *out = &g->tex[id];
    return g->texdata + g->tex[id].off;
}
#endif
