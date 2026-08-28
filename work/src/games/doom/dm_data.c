#include "dm_data.h"

extern const uint8_t doom_gfx[], doom_maps[];

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int dm_open(dgfx *g)
{
    const uint8_t *b = doom_gfx;
    if (b[0] != 'Z' || b[1] != 'D' || b[2] != 'G' || b[3] != '2') return 0;
    const uint8_t *h = b + 4;
    g->pal      = (const uint16_t *)(b + rd32(h));
    g->cmap     = b + rd32(h + 4);
    g->tex      = (const dtex *)(b + rd32(h + 8));
    g->texdata  = b + rd32(h + 12);
    g->ntex     = (int)rd32(h + 16);
    g->flattab  = (const uint32_t *)(b + rd32(h + 20));
    g->flatdata = b + rd32(h + 24);
    g->nflat    = (int)rd32(h + 28);
    g->skyflat  = (int)rd32(h + 32);
    g->skytex   = (int)rd32(h + 36);
    g->spr      = (const dspr *)(b + rd32(h + 40));
    g->sprdata  = b + rd32(h + 44);
    g->tinfo    = (const dtinfo *)(b + rd32(h + 48));
    g->ntinfo   = (int)rd32(h + 52);
    g->wpn      = (const dspr *)(b + rd32(h + 56));
    g->wpndata  = b + rd32(h + 60);
    g->nwpn     = (int)rd32(h + 64);
    const uint8_t *m = doom_maps;
    g->nmaps = (m[0] == 'Z' && m[1] == 'D' && m[2] == 'M' && m[3] == '1') ? (int)rd32(m + 4) : 0;
    return g->nmaps > 0;
}

int dm_load(const dgfx *g, dmap *m, int idx)
{
    if ((unsigned)idx >= (unsigned)g->nmaps) return 0;
    const uint8_t *b = doom_maps;
    const uint8_t *d = b + 8 + idx * 64;
    m->vert  = (const dvert *)(b + rd32(d));       m->nvert  = (int)rd32(d + 4);
    m->line  = (const dline *)(b + rd32(d + 8));   m->nline  = (int)rd32(d + 12);
    m->side  = (const dside *)(b + rd32(d + 16));  m->nside  = (int)rd32(d + 20);
    m->seg   = (const dseg  *)(b + rd32(d + 24));  m->nseg   = (int)rd32(d + 28);
    m->ssec  = (const dssec *)(b + rd32(d + 32));  m->nssec  = (int)rd32(d + 36);
    m->node  = (const dnode *)(b + rd32(d + 40));  m->nnode  = (int)rd32(d + 44);
    m->sec   = (const dsec  *)(b + rd32(d + 48));  m->nsec   = (int)rd32(d + 52);
    m->thing = (const dthing *)(b + rd32(d + 56)); m->nthing = (int)rd32(d + 60);
    return 1;
}
