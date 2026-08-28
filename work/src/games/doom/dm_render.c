/* dm_render.c — рендер как в оригинале: обход BSP спереди назад, стены
 * столбцами с перспективно верной текстурой, пол и потолок — обратным лучом
 * на пиксель.  Никаких visplane'ов: у нас есть кадровый буфер, и проще
 * закрашивать промежутки сразу, пока идёт стена. */
#include "dm_render.h"

#define CX 120
#define CY (VIEW_H / 2)
#define FOCAL 120           /* поле зрения 90 градусов по горизонтали */
#define NEARZ (16 << 8)

dview dv;
static const dgfx *g;
static const dmap *m;
/* Эти массивы читаются на каждый пиксель, поэтому им нельзя в .bss: он у нас
 * в RTC-памяти, а она медленнее обычного ОЗУ.  Берём их из арены. */
static int16_t *ceilclip, *floorclip;
static int16_t *rayx, *rayy;                  /* Q14 направление луча столбца */
static int32_t *ydist;                        /* Q12: FOCAL/(y-CY) */
static int view_bot;                          /* нижняя строка 3D-вида */
static int32_t *coldepth;                     /* глубина закрывшей столбец стены */
int16_t *dm_fh, *dm_ch;                       /* высоты секторов: карта во флеше, а их двигают */
static const band *bd;
static int sky_flat, sky_tex;   /* без начальных значений: .data должен быть пуст */
static int open_cols;           /* сколько столбцов экрана ещё не закрыты стеной */
uint32_t dm_c_plane, dm_c_wall, dm_c_seg, dm_n_seg, dm_c_box, dm_n_node, dm_c_all;
#ifdef PLAT_HOST
#define CYC() 0u
#else
extern uint32_t plat_cycles(void);
#define CYC() plat_cycles()
#endif

/* ---- мелочи ------------------------------------------------------------ */
static inline int dsin(int a) { return fx_sin_hi(a); }
static inline int dcos(int a) { return fx_sin_hi(a + 1024); }

int dm_sec_of_ssec(int ss)
{
    const dseg *sg = &m->seg[m->ssec[ss].first];
    int sn = m->line[sg->line].side[sg->side];
    return sn < 0 ? 0 : m->side[sn].sector;
}

int dm_point_ssec(int x, int y)          /* x,y — Q8 */
{
    if (!m->nnode) return 0;
    int n = m->nnode - 1;
    while (!(n & NF_SUBSECTOR)) {
        const dnode *nd = &m->node[n];
        int dx = x - (nd->x << 8), dy = y - (nd->y << 8);
        int64_t side = (int64_t)dy * nd->dx - (int64_t)dx * nd->dy;
        n = nd->child[side <= 0 ? 0 : 1];
    }
    return n & ~NF_SUBSECTOR;
}

/* ---- заливка пола и потолка -------------------------------------------- */
static void plane_run(int x, int y0, int y1, int planez, int flatid, int light)
{
    uint32_t c0 = CYC();
    if (y0 < bd->y0) y0 = bd->y0;
    if (y1 > bd->y1) y1 = bd->y1;
    if (y1 > view_bot) y1 = view_bot;
    if (y0 >= y1) { dm_c_plane += CYC() - c0; return; }
    if (flatid == sky_flat) {
        /* небо как в оригинале: столбец от угла взгляда (полный оборот — четыре
         * ширины текстуры), строка — прямо по экрану, без перспективы */
        const dtex *t;
        const uint8_t *tx = dm_tex(g, sky_tex, &t);
        int ang = dv.ang - (x - CX) * 1024 / (CX * 2);
        int col = ((ang * 4 * t->w) / 4096) & (t->w - 1);
        const uint8_t *src = tx + col * t->h;
        for (int y = y0; y < y1; y++) {
            int v = (y * t->h) / (VIEW_H - 30);
            if (v >= t->h) v = t->h - 1;
            band_row(bd, y)[x] = g->pal[src[v]];
        }
        dm_c_plane += CYC() - c0;
        return;
    }
    uint32_t off = g->flattab[flatid * 2], n = g->flattab[flatid * 2 + 1];
    const uint8_t *flat = g->flatdata + off;
    int nsh = n == 32 ? 5 : (n == 16 ? 4 : 6);      /* log2 стороны плитки */
    int shift = 6 - nsh;                            /* 64 юнита карты на плитку */
    int mask = n - 1;
    int dz = dv.z - planez;
    if (dz < 0) dz = -dz;
    if (dz > 65535) dz = 65535;                     /* дальше всё равно темно */
    int k = dz >> 4;                                /* Q4: держим произведения в 32 битах */
    int rx = rayx[x], ry = rayy[x];
    px *p = band_row(bd, y0) + x;
    for (int y = y0; y < y1; y++, p += SCR_W) {
        int32_t yd = ydist[y];
        if (!yd) continue;
        int dist = (k * yd) >> 12;                  /* Q8 вдоль оси взгляда */
        if (dist < 0) dist = -dist;
        int d6 = dist >> 6;
        int wx = dv.x + ((d6 * rx) >> 8);
        int wy = dv.y + ((d6 * ry) >> 8);
        int u = ((wx >> 8) >> shift) & mask;
        int v = ((-(wy >> 8)) >> shift) & mask;
        int lit = light + (dist >> 15);
        if (lit > 31) lit = 31;
        *p = g->pal[g->cmap[(lit << 8) + flat[(v << nsh) + u]]];
    }
    dm_c_plane += CYC() - c0;
}

/* ---- столбец стены ------------------------------------------------------ */
static void tex_column(int x, int y0, int y1, int top, int bot, int texid, int u,
                       int light, int yoff)
{
    uint32_t c0 = CYC();
    if (texid == 0xFFFF) return;
    const dtex *t;
    const uint8_t *tx = dm_tex(g, texid, &t);
    int a = y0 < bd->y0 ? bd->y0 : y0, b = y1 > bd->y1 ? bd->y1 : y1;
    if (b > view_bot) b = view_bot;
    if (a >= b || bot <= top) { dm_c_wall += CYC() - c0; return; }
    int hs = 0, ws = 0;
    while ((t->worig >> ws) > t->w) ws++;
    while ((t->horig >> hs) > t->h) hs++;
    int col = ((u >> ws) & (t->w - 1));
    const uint8_t *src = tx + col * t->h;
    const uint8_t *cm = g->cmap + (light << 8);
    int hstep = ((t->horig << 16) / (bot - top));
    for (int y = a; y < b; y++) {
        int v = (((y - top) * hstep) >> 16) + yoff;
        v = (v >> hs) & (t->h - 1);
        band_row(bd, y)[x] = g->pal[cm[src[v]]];
    }
    dm_c_wall += CYC() - c0;
}

/* ---- один seg ----------------------------------------------------------- */
static void draw_seg(const dseg *sg)
{
    uint32_t cseg = CYC();
    dm_n_seg++;
    const dline *ln = &m->line[sg->line];
    int sidenum = ln->side[sg->side];
    if (sidenum < 0) return;
    const dside *sd = &m->side[sidenum];
    const dsec *front = &m->sec[sd->sector];
    const dsec *back = 0;
    int backnum = ln->side[sg->side ^ 1];
    if ((ln->flags & ML_TWOSIDED) && backnum >= 0) back = &m->sec[m->side[backnum].sector];

    const dvert *v1 = &m->vert[sg->v1], *v2 = &m->vert[sg->v2];
    int dx1 = (v1->x << 8) - dv.x, dy1 = (v1->y << 8) - dv.y;
    int dx2 = (v2->x << 8) - dv.x, dy2 = (v2->y << 8) - dv.y;
    int c = dv.cos, s = dv.sin;
    int tx1 = (int)(((int64_t)dx1 * s - (int64_t)dy1 * c) >> 14);
    int tz1 = (int)(((int64_t)dx1 * c + (int64_t)dy1 * s) >> 14);
    int tx2 = (int)(((int64_t)dx2 * s - (int64_t)dy2 * c) >> 14);
    int tz2 = (int)(((int64_t)dx2 * c + (int64_t)dy2 * s) >> 14);
    if (tz1 < NEARZ && tz2 < NEARZ) return;

    int u1 = 0, u2 = fx_hypot((v2->x - v1->x), (v2->y - v1->y));   /* длина в юнитах */
    u1 += sg->off; u2 += sg->off;
    /* обрезка по ближней плоскости */
    if (tz1 < NEARZ) {
        int t = ((NEARZ - tz1) << 12) / (tz2 - tz1);
        tx1 += ((tx2 - tx1) * t) >> 12; u1 += ((u2 - u1) * t) >> 12; tz1 = NEARZ;
    } else if (tz2 < NEARZ) {
        int t = ((NEARZ - tz2) << 12) / (tz1 - tz2);
        tx2 += ((tx1 - tx2) * t) >> 12; u2 += ((u1 - u2) * t) >> 12; tz2 = NEARZ;
    }
    int sx1 = CX + tx1 * CX / tz1;
    int sx2 = CX + tx2 * CX / tz2;
    if (sx1 >= sx2) return;                       /* стена повёрнута к нам спиной */
    if (sx2 <= 0 || sx1 >= SCR_W) return;

    int iz1 = (1 << 22) / tz1, iz2 = (1 << 22) / tz2;      /* Q22/z */
    int uz1 = (u1 * iz1) >> 8, uz2 = (u2 * iz2) >> 8;
    int span = sx2 - sx1;
    int x0 = sx1 < 0 ? 0 : sx1, x1 = sx2 > SCR_W ? SCR_W : sx2;
    int any = 0;
    for (int x = x0; x < x1; x++) if (ceilclip[x] + 1 < floorclip[x]) { any = 1; break; }
    if (!any) { dm_c_seg += CYC() - cseg; return; }   /* всё за уже нарисованными стенами */
    (void)0;
    int light = 31 - (front->light >> 3);
    if (light < 0) light = 0;
    if (light > 31) light = 31;
    int fz = dm_fh[sd->sector] << 8, cz = dm_ch[sd->sector] << 8;
    int bsec = back ? m->side[backnum].sector : 0;
    int bfz = back ? dm_fh[bsec] << 8 : 0, bcz = back ? dm_ch[bsec] << 8 : 0;
    int upper = sd->top, lower = sd->bot, mid = sd->mid;
    /* высоты на экране: ytop = CY - (h * FOCAL) / z, а 1/z у нас уже есть */
    int ctop = ((cz - dv.z) * FOCAL) >> 8, cbot = ((fz - dv.z) * FOCAL) >> 8;
    int btop = back ? ((bcz - dv.z) * FOCAL) >> 8 : 0;
    int bbot = back ? ((bfz - dv.z) * FOCAL) >> 8 : 0;
    int sky_front = front->cpic == sky_flat;

    for (int x = x0; x < x1; x++) {
        int t = ((x - sx1) << 12) / span;
        int iz = iz1 + (((iz2 - iz1) * t) >> 12);
        if (iz <= 0) continue;
        int uz = uz1 + (((uz2 - uz1) * t) >> 12);
        int u = (uz << 8) / iz;                       /* единственное деление на столбец */
        int ytop = CY - ((ctop * iz) >> 14);
        int ybot = CY - ((cbot * iz) >> 14);
        int lit = light + (((1 << 22) / iz) >> 15);
        if (lit > 31) lit = 31;

        int ct = ceilclip[x] + 1, cb = floorclip[x];
        if (ct >= cb) continue;
        /* потолок и пол в промежутках */
        int wt = ytop < ct ? ct : ytop, wb = ybot > cb ? cb : ybot;
        plane_run(x, ct, wt, cz, front->cpic, light);
        plane_run(x, wb, cb, fz, front->fpic, light);

        if (!back) {                                  /* глухая стена */
            tex_column(x, wt, wb, ytop, ybot, mid, u, lit,
                       (ln->flags & ML_DONTPEGBOTTOM) ? (fz - cz) >> 8 : 0);
            if (ceilclip[x] + 1 < floorclip[x]) open_cols--;
            ceilclip[x] = VIEW_H; floorclip[x] = 0;    /* столбец закрыт */
            coldepth[x] = (1 << 22) / iz;
        } else {
            int bytop = CY - ((btop * iz) >> 14);
            int bybot = CY - ((bbot * iz) >> 14);
            if (bcz < cz && !(sky_front && back->cpic == sky_flat)) {
                int a = wt, b = bytop > cb ? cb : bytop;
                tex_column(x, a, b, ytop, bytop, upper, u, lit,
                           (ln->flags & ML_DONTPEGTOP) ? 0 : (bcz - cz) >> 8);
                if (b > ceilclip[x]) ceilclip[x] = (int16_t)(b - 1);
            } else if (bytop > ceilclip[x] + 1 && bcz < cz) {
                ceilclip[x] = (int16_t)(bytop - 1);
            }
            if (bfz > fz) {
                int a = bybot < ct ? ct : bybot, b = wb;
                tex_column(x, a, b, bybot, ybot, lower, u, lit,
                           (ln->flags & ML_DONTPEGBOTTOM) ? (cz - bfz) >> 8 : 0);
                if (a - 1 < floorclip[x]) floorclip[x] = (int16_t)a;
            }
            if (ytop > ceilclip[x] + 1 && bcz >= cz) { }
            if (ceilclip[x] < wt - 1) ceilclip[x] = (int16_t)(wt - 1);
            if (floorclip[x] > wb) floorclip[x] = (int16_t)wb;
        }
    }
    dm_c_seg += CYC() - cseg;
}

/* ---- обход дерева ------------------------------------------------------- */
/* Прямоугольник узла: переводим четыре угла в вид, ищем полосу экрана, куда он
 * может попасть.  Если полоса пуста или целиком закрыта — ветку не смотрим. */
static int box_visible(const int16_t *bb)
{
    uint32_t c0 = CYC();
    dm_n_node++;
    int top = bb[0], bot = bb[1], left = bb[2], right = bb[3];
    int px_ = dv.x >> 8, py = dv.y >> 8;
    if (px_ >= left && px_ <= right && py >= bot && py <= top) { dm_c_box += CYC() - c0; return 1; }
    static const int8_t cx[4] = { 0, 0, 1, 1 }, cy[4] = { 0, 1, 0, 1 };
    int minx = SCR_W, maxx = -1, anyfront = 0;
    for (int i = 0; i < 4; i++) {
        int wx = cx[i] ? right : left, wy = cy[i] ? top : bot;
        int dx = (wx << 8) - dv.x, dy = (wy << 8) - dv.y;
        int tx = (int)(((int64_t)dx * dv.sin - (int64_t)dy * dv.cos) >> 14);
        int tz = (int)(((int64_t)dx * dv.cos + (int64_t)dy * dv.sin) >> 14);
        if (tz < NEARZ) {                     /* угол за спиной — полоса шире экрана */
            if (tx < 0) minx = 0; else maxx = SCR_W - 1;
            anyfront = anyfront || tz > -NEARZ;
            continue;
        }
        anyfront = 1;
        int sx = CX + tx * CX / tz;
        if (sx < minx) minx = sx;
        if (sx > maxx) maxx = sx;
    }
    if (!anyfront) { dm_c_box += CYC() - c0; return 0; }
    if (maxx < 0 || minx >= SCR_W) { dm_c_box += CYC() - c0; return 0; }
    if (minx < 0) minx = 0;
    if (maxx >= SCR_W) maxx = SCR_W - 1;
    for (int x = minx; x <= maxx; x++)
        if (ceilclip[x] + 1 < floorclip[x]) { dm_c_box += CYC() - c0; return 1; }
    dm_c_box += CYC() - c0;
    return 0;
}

static void render_ssec(int ss)
{
    const dssec *s = &m->ssec[ss];
    for (int i = 0; i < s->count && open_cols; i++) draw_seg(&m->seg[s->first + i]);
}

static void render_node(int n)
{
    if (!open_cols) return;
    if (n & NF_SUBSECTOR) { render_ssec(n & ~NF_SUBSECTOR); return; }
    const dnode *nd = &m->node[n];
    int dx = dv.x - (nd->x << 8), dy = dv.y - (nd->y << 8);
    int64_t side = (int64_t)dy * nd->dx - (int64_t)dx * nd->dy;
    int near = side <= 0 ? 0 : 1;
    render_node(nd->child[near]);
    if (open_cols && box_visible(nd->bbox[near ^ 1])) render_node(nd->child[near ^ 1]);
}

int dm_bind(const dgfx *gg, const dmap *mm)
{
    g = gg; m = mm;
    dm_fh = mem_alloc(m->nsec * 2);
    dm_ch = mem_alloc(m->nsec * 2);
    if (!dm_fh || !dm_ch) return 0;
    for (int i = 0; i < m->nsec; i++) { dm_fh[i] = m->sec[i].floor; dm_ch[i] = m->sec[i].ceil; }
    return 1;
}

void dm_render(const band *b)
{
    bd = b;
    dv.sin = dsin(dv.ang); dv.cos = dcos(dv.ang);
    dm_c_plane = dm_c_wall = dm_c_seg = dm_n_seg = dm_c_box = dm_n_node = 0;
    uint32_t call0 = CYC();
    open_cols = SCR_W;
    for (int x = 0; x < SCR_W; x++) {
        ceilclip[x] = -1; floorclip[x] = VIEW_H; coldepth[x] = 0x7FFFFFFF;
        int tx = (x - CX);
        rayx[x] = (int16_t)(dv.cos + (-dv.sin * tx) / CX);   /* влезает в 32 бита */
        rayy[x] = (int16_t)(dv.sin + (dv.cos * tx) / CX);
    }
    for (int y = 0; y < VIEW_H; y++) {
        int d = y - CY;
        ydist[y] = d ? (FOCAL << 12) / d : 0;       /* Q12: k(Q4) * ydist -> Q8 после >>12 */
    }
    render_node(m->nnode - 1);
    dm_c_all = CYC() - call0;
}

int dm_render_init(int skyflat, int skytex)
{
    sky_flat = skyflat; sky_tex = skytex;
    view_bot = VIEW_H;
    ceilclip = mem_alloc(SCR_W * 2); floorclip = mem_alloc(SCR_W * 2);
    rayx = mem_alloc(SCR_W * 2); rayy = mem_alloc(SCR_W * 2);
    ydist = mem_alloc(VIEW_H * 4);
    coldepth = mem_alloc(SCR_W * 4);
    return ceilclip && floorclip && rayx && rayy && ydist && coldepth;
}

/* ---- вещи: плоские картинки, всегда лицом к игроку ---------------------- */
void dm_sprite(int wx, int wy, int wz, int sprid, int light)
{
    const dspr *sp;
    const uint8_t *pix = dm_sprpix(g, sprid, &sp);
    if (sp->w < 2) return;
    int dx = wx - dv.x, dy = wy - dv.y;
    int tx = (int)(((int64_t)dx * dv.sin - (int64_t)dy * dv.cos) >> 14);
    int tz = (int)(((int64_t)dx * dv.cos + (int64_t)dy * dv.sin) >> 14);
    if (tz < NEARZ) return;
    int iz = (1 << 22) / tz;
    /* спрайт вдвое мельче оригинала, поэтому пиксель = 2 юнита карты */
    int wpx = sp->w * 2, hpx = sp->h * 2;
    int left = tx - sp->xoff * 2 * 256, right = left + wpx * 256;
    int sxl = CX + (left / 64) * CX / (tz / 64);      /* без 64-битного деления */
    int sxr = CX + (right / 64) * CX / (tz / 64);
    if (sxr <= sxl || sxr <= 0 || sxl >= SCR_W) return;
    int topz = wz + sp->yoff * 2 * 256;
    int ytop = CY - ((((topz - dv.z) * FOCAL) >> 8) * iz >> 14);
    int ybot = CY - (((((topz - hpx * 256) - dv.z) * FOCAL) >> 8) * iz >> 14);
    if (ybot <= ytop) return;
    int hstep = (sp->h << 16) / (ybot - ytop);
    int x0 = sxl < 0 ? 0 : sxl, x1 = sxr > SCR_W ? SCR_W : sxr;
    int lit = light + (tz >> 15);
    if (lit > 31) lit = 31;
    if (lit < 0) lit = 0;
    const uint8_t *cm = g->cmap + (lit << 8);
    for (int x = x0; x < x1; x++) {
        if (coldepth[x] <= tz) continue;             /* закрыто стеной */
        int u = (x - sxl) * sp->w / (sxr - sxl);
        const uint8_t *col = pix + u * sp->h;
        int a = ytop < 0 ? 0 : ytop, b = ybot > view_bot ? view_bot : ybot;
        px *p = band_row(bd, a) + x;
        for (int y = a; y < b; y++, p += SCR_W) {
            uint8_t t = col[((y - ytop) * hstep) >> 16];
            if (t != 255) *p = g->pal[cm[t]];
        }
    }
}

void dm_frame_begin(void) { }

/* ---- оружие в руках ----------------------------------------------------- */
void dm_weapon(int frame, int bob)
{
    if (frame >= g->nwpn) return;
    const dspr *sp = &g->wpn[frame];
    if (sp->w < 2) return;
    const uint8_t *pix = g->wpndata + sp->off;
    /* спрайт нарисован под экран 320x200: ставим по центру и прижимаем к низу */
    int x0 = SCR_W / 2 - sp->w / 2;
    int y0 = view_bot - sp->h + bob;
    for (int x = 0; x < sp->w; x++) {
        int sx = x0 + x;
        if ((unsigned)sx >= SCR_W) continue;
        const uint8_t *col = pix + x * sp->h;
        for (int y = 0; y < sp->h; y++) {
            int sy = y0 + y;
            if (sy < bd->y0 || sy >= bd->y1 || sy >= view_bot) continue;
            uint8_t t = col[y];
            if (t != 255) band_row(bd, sy)[sx] = g->pal[t];
        }
    }
}
