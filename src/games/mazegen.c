/* mazegen.c — поиск в глубину с возвратом: один проход, идеальный лабиринт. */
#include "mazegen.h"
#include "fx.h"

int maze_make(maze_t *m, int w, int h)
{
    m->w = w; m->h = h; m->gw = 2 * w + 1; m->gh = 2 * h + 1;
    m->g = mem_alloc(m->gw * m->gh);
    uint16_t *stack = mem_alloc(w * h * 2);
    uint8_t *seen = mem_alloc(w * h);
    if (!m->g || !stack || !seen) return 0;
    for (int i = 0; i < m->gw * m->gh; i++) m->g[i] = 1;

    int sp = 0, cx = 0, cy = 0;
    seen[0] = 1;
    m->g[1 * m->gw + 1] = 0;
    stack[sp++] = 0;
    while (sp) {
        int dirs[4], n = 0;
        static const int8_t dx[4] = { 0, 0, -1, 1 }, dy[4] = { -1, 1, 0, 0 };
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[ny * w + nx]) continue;
            dirs[n++] = d;
        }
        if (!n) {
            int c = stack[--sp];
            cx = c % w; cy = c / w;
            continue;
        }
        int d = dirs[plat_rand() % (unsigned)n];
        int nx = cx + dx[d], ny = cy + dy[d];
        m->g[(2 * cy + 1 + dy[d]) * m->gw + (2 * cx + 1 + dx[d])] = 0;
        m->g[(2 * ny + 1) * m->gw + (2 * nx + 1)] = 0;
        seen[ny * w + nx] = 1;
        stack[sp++] = (uint16_t)(cy * w + cx);
        cx = nx; cy = ny;
    }
    m->ex = 2 * (w - 1) + 1; m->ey = 2 * (h - 1) + 1;
    return 1;
}
