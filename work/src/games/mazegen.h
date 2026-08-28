#ifndef MAZEGEN_H
#define MAZEGEN_H
#include "plat.h"
/* сетка (2w+1) x (2h+1): 1 — стена, 0 — проход */
typedef struct { int w, h, gw, gh; uint8_t *g; int ex, ey; } maze_t;
int  maze_make(maze_t *m, int w, int h);   /* память из mem_alloc */
static inline int maze_at(const maze_t *m, int x, int y)
{ return (unsigned)x >= (unsigned)m->gw || (unsigned)y >= (unsigned)m->gh ? 1 : m->g[y * m->gw + x]; }
#endif
