/* doom.c — игрок, столкновения и связка с движком. */
#include "game.h"
#include "snd.h"
#include "dm_render.h"

extern const snote doom_music[];
extern const int doom_music_len;


#define EYE   (41 << 8)
#define RADIUS (18 << 8)
#define STEPUP (24 << 8)

static dgfx g;
static dmap m;

/* вещи на уровне: 20 байт на штуку */
typedef struct {
    int32_t x, y, z;      /* Q8 */
    int16_t hp;
    uint8_t ti;           /* индекс в g.tinfo */
    uint8_t gone;         /* 1 = убрано с карты совсем */
    uint16_t ang;         /* 0..4095 */
    uint8_t dstate;       /* 0 жив, иначе номер кадра смерти (последний = труп) */
    uint8_t cool;         /* кадров до следующей атаки / смены кадра смерти */
} mobj_t;

#define MAXMOBJ  400      /* на E1M1 набирается около двухсот */
#define MAXDRAW  48       /* столько ближайших вещей рисуем за кадр */

static mobj_t *mobjs;
static int nmobj, mobj_cap;
static int thing_blocked(int nx, int ny, int radius, int skip);
static int flash_until, dead, level_done, hurt_until;

/* Движущиеся секторы: двери, лифты, платформы.  Больше восьми сразу на
 * маленькой карте не бывает. */
typedef struct {
    int16_t sec;
    int16_t target, speed;    /* куда и с какой скоростью едет потолок/пол */
    uint8_t kind;             /* 0 нет, 1 дверь (потолок), 2 лифт (пол) */
    uint8_t stage;            /* 0 едет туда, 1 ждёт, 2 едет обратно */
    int16_t back;             /* высота, куда вернуться */
    uint32_t wait_until;
} moving_t;
static moving_t movers[8];

static int sector_lowest_ceil(int sec)
{
    int lo = 32767;
    for (int i = 0; i < m.nline; i++) {
        const dline *ln = &m.line[i];
        for (int sd = 0; sd < 2; sd++) {
            if (ln->side[sd] < 0) continue;
            if (m.side[ln->side[sd]].sector != sec) continue;
            int other = ln->side[sd ^ 1];
            if (other < 0) continue;
            int os = m.side[other].sector;
            if (dm_ch[os] < lo) lo = dm_ch[os];
        }
    }
    return lo == 32767 ? dm_ch[sec] : lo;
}

static int sector_lowest_floor(int sec)
{
    int lo = 32767;
    for (int i = 0; i < m.nline; i++) {
        const dline *ln = &m.line[i];
        for (int sd = 0; sd < 2; sd++) {
            if (ln->side[sd] < 0) continue;
            if (m.side[ln->side[sd]].sector != sec) continue;
            int other = ln->side[sd ^ 1];
            if (other < 0) continue;
            int os = m.side[other].sector;
            if (dm_fh[os] < lo) lo = dm_fh[os];
        }
    }
    return lo == 32767 ? dm_fh[sec] : lo;
}

static void start_move(int sec, int kind, int target, int speed, int back, int wait)
{
    for (int i = 0; i < 8; i++) if (movers[i].kind && movers[i].sec == sec) return;
    SND(snd_door);
    for (int i = 0; i < 8; i++) {
        if (movers[i].kind) continue;
        movers[i].sec = (int16_t)sec; movers[i].kind = (uint8_t)kind;
        movers[i].target = (int16_t)target; movers[i].speed = (int16_t)speed;
        movers[i].back = (int16_t)back; movers[i].stage = 0;
        movers[i].wait_until = (uint32_t)wait;
        return;
    }
}

static void run_movers(void)
{
    for (int i = 0; i < 8; i++) {
        moving_t *mv = &movers[i];
        if (!mv->kind) continue;
        int16_t *h = mv->kind == 1 ? &dm_ch[mv->sec] : &dm_fh[mv->sec];
        int goal = mv->stage == 2 ? mv->back : mv->target;
        if (mv->stage == 1) {
            if (now_ms() >= mv->wait_until) mv->stage = 2;
            continue;
        }
        if (*h < goal) { *h += mv->speed; if (*h > goal) *h = (int16_t)goal; }
        else if (*h > goal) { *h -= mv->speed; if (*h < goal) *h = (int16_t)goal; }
        else if (mv->stage == 0) {
            if (mv->wait_until) { mv->stage = 1; mv->wait_until = now_ms() + mv->wait_until; }
            else mv->kind = 0;
        } else mv->kind = 0;
    }
}

/* Нажали на линию со специальным номером — открываем дверь, зовём лифт. */
static int use_line(const dline *ln)
{
    int sp = ln->special;
    if (!sp) return 0;
    int back = ln->side[1] >= 0 ? m.side[ln->side[1]].sector : -1;
    switch (sp) {
    case 1: case 26: case 27: case 28: case 31: case 32: case 33: case 34:
    case 117: case 118:                       /* двери */
        if (back < 0) return 0;
        start_move(back, 1, sector_lowest_ceil(back) - 4, sp >= 117 ? 8 : 4,
                   dm_fh[back], (sp == 1 || sp == 117 || sp == 26 || sp == 27 || sp == 28) ? 4000 : 0);
        return 1;
    case 62: case 21: case 88: case 10:       /* лифты вниз */
        if (back < 0) return 0;
        start_move(back, 2, sector_lowest_floor(back), 4, dm_fh[back], 3000);
        return 1;
    case 11: case 51: case 52: case 124:      /* выход с уровня */
        return 2;
    default:
        if (back >= 0) {                      /* остальное считаем дверью */
            start_move(back, 1, sector_lowest_ceil(back) - 4, 4, dm_fh[back], 4000);
            return 1;
        }
    }
    return 0;
}
static int cur_map, health, armor, ammo;
static uint32_t started;

static const dmap *M(void) { return &m; }

static int seg_blocks(const dseg *sg, int *stepz)
{
    const dline *ln = &m.line[sg->line];
    int fs = ln->side[sg->side], bs = ln->side[sg->side ^ 1];
    if (fs < 0 || bs < 0 || !(ln->flags & ML_TWOSIDED)) return 1;
    if (ln->flags & ML_BLOCKING) return 1;
    int as = m.side[fs].sector, bsn = m.side[bs].sector;
    int fl = imax(dm_fh[as], dm_fh[bsn]), ce = imin(dm_ch[as], dm_ch[bsn]);
    if ((ce - fl) < 56) return 1;                    /* не пролезть */
    *stepz = dm_fh[bsn] << 8;
    return 0;
}

/* расстояние от точки до отрезка (в Q8), знак не важен */
static int seg_dist(const dseg *sg, int x, int y)
{
    const dvert *v1 = &m.vert[sg->v1], *v2 = &m.vert[sg->v2];
    int ax = v1->x << 8, ay = v1->y << 8, bx = v2->x << 8, by = v2->y << 8;
    int dx = (bx - ax) >> 8, dy = (by - ay) >> 8;
    int len2 = dx * dx + dy * dy;
    if (!len2) return fx_hypot((x - ax) >> 8, (y - ay) >> 8) << 8;
    int t = (((x - ax) >> 8) * dx + ((y - ay) >> 8) * dy) / (len2 ? len2 : 1);   /* 0..1 в долях */
    t = iclamp(t, 0, 256) * 0 + iclamp((((x - ax) >> 8) * dx + ((y - ay) >> 8) * dy) * 256
                                       / (len2 ? len2 : 1), 0, 256);
    int px_ = ax + dx * t, py = ay + dy * t;          /* dx в юнитах, t в 1/256 */
    return fx_hypot((x - px_) >> 8, (y - py) >> 8) << 8;
}

static int try_move(int nx, int ny)
{
    int ss = dm_point_ssec(nx, ny);
    const dssec *s = &m.ssec[ss];
    int floorz = dm_fh[dm_sec_of_ssec(ss)] << 8;
    for (int i = 0; i < s->count; i++) {
        const dseg *sg = &m.seg[s->first + i];
        int stepz = floorz;
        int blocking = seg_blocks(sg, &stepz);
        if (!blocking && stepz - floorz > STEPUP) blocking = 1;
        if (!blocking) continue;
        if (seg_dist(sg, nx, ny) < RADIUS) {
            if (m.line[sg->line].special &&
                use_line(&m.line[sg->line]) == 2) { level_done = 1; SND(snd_win); }
            return 0;
        }
    }
    if (floorz - (dv.z - EYE) > STEPUP) return 0;
    if (thing_blocked(nx, ny, RADIUS >> 8, -1)) return 0;
    dv.x = nx; dv.y = ny;
    dv.z = floorz + EYE;
    return 1;
}

/* Флаги вещи в WAD: 1/2/4 — на каком уровне сложности она есть, 16 — только
 * для сетевой игры.  Берём «средний», как оригинал на третьей сложности: иначе
 * в одну точку встают сразу три копии одного монстра. */
#define TH_SKILL 2
#define TH_MULTI 16

static int thing_wanted(const dthing *t)
{
    if (t->flags & TH_MULTI) return 0;
    if ((t->flags & 7) && !(t->flags & TH_SKILL)) return 0;
    return dm_tinfo(&g, t->type) != 0;
}

/* Арена ничего не возвращает, поэтому место под вещи берётся один раз за
 * запуск: иначе каждая смерть отъедала бы по восемь килобайт, и через
 * несколько попыток вещей на карте не оставалось вовсе. */
static void spawn_things(void)
{
    int want = 0;
    for (int i = 0; i < m.nthing; i++) if (thing_wanted(&m.thing[i])) want++;
    if (want > MAXMOBJ) want = MAXMOBJ;
    if (want > mobj_cap) {
        mobj_t *p = mem_alloc(want * (int)sizeof(mobj_t));
        if (p) { mobjs = p; mobj_cap = want; }
    }
    nmobj = 0;
    if (!mobjs) return;
    for (int i = 0; i < m.nthing && nmobj < mobj_cap; i++) {
        const dthing *t = &m.thing[i];
        if (!thing_wanted(t)) continue;
        const dtinfo *ti = dm_tinfo(&g, t->type);
        mobj_t *o = &mobjs[nmobj];
        o->x = t->x << 8; o->y = t->y << 8;
        int ss = dm_point_ssec(o->x, o->y);
        o->z = dm_fh[dm_sec_of_ssec(ss)] << 8;
        o->hp = ti->hp;
        o->ti = (uint8_t)(ti - g.tinfo);
        o->gone = 0; o->dstate = 0; o->cool = 0;
        o->ang = (uint16_t)((t->angle * 4096 / 360) & 4095);
        nmobj++;
    }
}

/* Рисуем вещи после стен: дальние раньше ближних, иначе они перекроют друг
 * друга не в том порядке.  Мест в списке меньше, чем вещей на карте, поэтому
 * берём ближайшие из тех, что вообще могут попасть на экран: раньше брались
 * просто первые по порядку в WAD — и монстры почти никогда в них не попадали. */
static void draw_things(void)
{
    int16_t order[MAXDRAW];
    int32_t dist[MAXDRAW];
    int n = 0;
    for (int i = 0; i < nmobj; i++) {
        mobj_t *o = &mobjs[i];
        if (o->gone) continue;
        int dx = o->x - dv.x, dy = o->y - dv.y;
        int tz = (int)(((int64_t)dx * dv.cos + (int64_t)dy * dv.sin) >> 14);
        if (tz < (16 << 8)) continue;                          /* за спиной */
        int tx = (int)(((int64_t)dx * dv.sin - (int64_t)dy * dv.cos) >> 14);
        if (tx > tz + (96 << 8) || tx < -tz - (96 << 8)) continue;   /* мимо экрана */
        int gx = dx >> 8, gy = dy >> 8;
        int d = gx * gx + gy * gy;
        if (d > 2600 * 2600) continue;
        if (n == MAXDRAW) {
            if (d >= dist[0]) continue;                        /* дальше всех отобранных */
            int k = 0;                                         /* самый дальний уступает место */
            while (k + 1 < MAXDRAW && dist[k + 1] > d) {
                dist[k] = dist[k + 1]; order[k] = order[k + 1]; k++;
            }
            dist[k] = d; order[k] = (int16_t)i;
        } else {
            int k = n++;
            while (k > 0 && dist[k - 1] < d) { dist[k] = dist[k - 1]; order[k] = order[k - 1]; k--; }
            dist[k] = d; order[k] = (int16_t)i;
        }
    }
    for (int k = 0; k < n; k++) {
        mobj_t *o = &mobjs[order[k]];
        const dtinfo *ti = &g.tinfo[o->ti];
        int id;
        if (o->dstate) {                                       /* падает или лежит */
            int f = o->dstate - 1;
            if (f >= ti->ndeath) f = ti->ndeath - 1;
            id = ti->deathbase + f;
        } else {
            int frame = ti->nframes > 1 ? (int)((now_ms() / 180) % ti->nframes) : 0;
            int rot = 0;
            if (ti->nrots > 1) {
                int a = fx_atan2((o->y - dv.y) >> 8, (o->x - dv.x) >> 8);   /* 0..1023 */
                int rel = ((o->ang >> 2) - a + 512 + 64) & 1023;            /* +22.5 градуса */
                rot = (rel * 8 / 1024) & 7;
            }
            id = ti->sprbase + frame * ti->nrots + rot;
        }
        int ss = dm_point_ssec(o->x, o->y);
        int light = 31 - (m.sec[dm_sec_of_ssec(ss)].light >> 3);
        dm_sprite(o->x, o->y, o->z, id, iclamp(light, 0, 31));
    }
}

/* подбираем всё, на что наступили */
static void pickups(void)
{
    for (int i = 0; i < nmobj; i++) {
        mobj_t *o = &mobjs[i];
        if (o->gone || o->dstate) continue;
        const dtinfo *ti = &g.tinfo[o->ti];
        if (!(ti->flags & TF_PICKUP)) continue;
        int dx = (o->x - dv.x) >> 8, dy = (o->y - dv.y) >> 8;
        if (dx * dx + dy * dy > 24 * 24) continue;
        o->gone = 1;
        SND(snd_pick);
        switch (ti->type) {
        case 2011: health = imin(health + 10, 100); break;
        case 2012: health = imin(health + 25, 100); break;
        case 2014: health = imin(health + 1, 200); break;
        case 2013: health = imin(health + 100, 200); break;
        case 2015: armor = imin(armor + 1, 200); break;
        case 2018: armor = imax(armor, 100); break;
        case 2019: armor = imax(armor, 200); break;
        case 2007: ammo = imin(ammo + 10, 200); break;
        case 2048: ammo = imin(ammo + 50, 200); break;
        case 2008: ammo = imin(ammo + 8, 200); break;
        case 2049: ammo = imin(ammo + 20, 200); break;
        default: ammo = imin(ammo + 5, 200); break;
        }
    }
}

static void spawn(void)
{
    for (int i = 0; i < m.nthing; i++) {
        if (m.thing[i].type != 1) continue;
        dv.x = m.thing[i].x << 8;
        dv.y = m.thing[i].y << 8;
        dv.ang = (m.thing[i].angle * 4096 / 360) & 4095;
        int ss = dm_point_ssec(dv.x, dv.y);
        dv.z = (dm_fh[dm_sec_of_ssec(ss)] << 8) + EYE;
        return;
    }
    dv.x = dv.y = 0; dv.z = EYE; dv.ang = 0;
}

/* панель состояния в духе оригинала */
/* ---- бой ---------------------------------------------------------------- */

/* Монстры, бочки и колонны занимают место: сквозь них не пройти.  Трупы и
 * подбираемое не мешают. */
static int thing_blocked(int nx, int ny, int radius, int skip)
{
    for (int i = 0; i < nmobj; i++) {
        const mobj_t *o = &mobjs[i];
        if (i == skip || o->gone || o->dstate) continue;
        const dtinfo *ti = &g.tinfo[o->ti];
        if (!(ti->flags & (TF_ENEMY | TF_SOLID))) continue;
        int dx = (nx - o->x) >> 8, dy = (ny - o->y) >> 8;
        int r = ti->radius + radius;
        if (dx * dx + dy * dy < r * r) return 1;
    }
    return 0;
}

static int mobj_blocked(int nx, int ny, int radius)
{
    int ss = dm_point_ssec(nx, ny);
    const dssec *sc = &m.ssec[ss];
    for (int i = 0; i < sc->count; i++) {
        const dseg *sg = &m.seg[sc->first + i];
        int stepz = 0;
        if (!seg_blocks(sg, &stepz)) continue;
        if (seg_dist(sg, nx, ny) < (radius << 8)) return 1;
    }
    return 0;
}

/* Выстрел: шагаем лучом вперёд и смотрим, кто попался. */
static void fire(void)
{
    if (ammo <= 0) return;
    ammo--;
    flash_until = (int)now_ms() + 90;
    SND(snd_shot);
    int dx = fx_sin_hi(dv.ang + 1024), dy = fx_sin_hi(dv.ang);
    for (int d = 32; d < 1600; d += 16) {
        int px_ = dv.x + (int)(((int64_t)dx * d * 256) >> 14);
        int py  = dv.y + (int)(((int64_t)dy * d * 256) >> 14);
        if (mobj_blocked(px_, py, 2)) return;            /* попали в стену */
        for (int i = 0; i < nmobj; i++) {
            mobj_t *o = &mobjs[i];
            if (o->gone || o->hp <= 0) continue;
            const dtinfo *ti = &g.tinfo[o->ti];
            if (!(ti->flags & (TF_ENEMY | TF_SOLID))) continue;
            int ox = (o->x - px_) >> 8, oy = (o->y - py) >> 8;
            if (ox * ox + oy * oy > ti->radius * ti->radius) continue;
            o->hp -= 12 + (int)(plat_rand() % 12);
            if (o->hp > 0) { SND(snd_hit); return; }
            o->hp = 0;                                   /* пал */
            if (ti->ndeath) { o->dstate = 1; o->cool = 3; SND(snd_die); }
            else { o->gone = 1; SND(snd_hit); }
            return;
        }
    }
}

/* Видит ли монстр игрока: шагаем лучом до него и смотрим, не мешает ли стена.
 * Считается редко — только когда монстр уже собрался стрелять. */
static int sees_player(const mobj_t *o)
{
    int dx = dv.x - o->x, dy = dv.y - o->y;
    for (int t = 16; t < 256; t += 16) {        /* пятнадцать проб вдоль отрезка */
        int px_ = o->x + ((dx * t) >> 8);
        int py  = o->y + ((dy * t) >> 8);
        if (mobj_blocked(px_, py, 2)) return 0;
    }
    return 1;
}

/* Простой ИИ: заметил — идёт на игрока и постреливает.  Откат у каждого свой,
 * иначе на всю карту приходился один выстрел в секунду. */
static void think_enemies(void)
{
    for (int i = 0; i < nmobj; i++) {
        mobj_t *o = &mobjs[i];
        if (o->gone) continue;
        const dtinfo *ti = &g.tinfo[o->ti];
        if (o->dstate) {                                  /* доигрываем падение */
            if (o->dstate < ti->ndeath) {
                if (o->cool) o->cool--; else { o->dstate++; o->cool = 3; }
            }
            continue;
        }
        if (!(ti->flags & TF_ENEMY) || o->hp <= 0) continue;
        if (o->cool) o->cool--;
        int dx = (dv.x - o->x) >> 8, dy = (dv.y - o->y) >> 8;
        int d2 = dx * dx + dy * dy;
        if (d2 > 900 * 900) continue;
        int a = fx_atan2(dy, dx);                         /* 0..1023 */
        o->ang = (uint16_t)((a << 2) & 4095);
        if (d2 > 60 * 60) {                               /* подходим ближе */
            int step = 900;
            int nx = o->x + ((fx_sin_hi(o->ang + 1024) * step) >> 14);
            int ny = o->y + ((fx_sin_hi(o->ang) * step) >> 14);
            if (!mobj_blocked(nx, ny, ti->radius) && !thing_blocked(nx, ny, ti->radius, i)) {
                o->x = nx; o->y = ny;
                int ss = dm_point_ssec(o->x, o->y);
                o->z = dm_fh[dm_sec_of_ssec(ss)] << 8;
            }
        }
        if (d2 < 700 * 700 && !o->cool && !(plat_rand() % 20) && sees_player(o)) {
            o->cool = 30;                                 /* полторы секунды на 20 кадрах */
            int dmg = 2 + (int)(plat_rand() % 6);
            if (armor > 0) { armor -= dmg / 2; if (armor < 0) armor = 0; dmg -= dmg / 3; }
            health -= dmg;
            hurt_until = (int)now_ms() + 120;
            SND(snd_hurt);
            if (health <= 0) { health = 0; dead = 1; SND(snd_die); }
        }
    }
}

static void hud(const band *b)
{
    char s[16];
    int y = VIEW_H;
    gfx_fill(b, 0, y, SCR_W, SCR_H - y, RGB(46, 34, 26));
    gfx_fill(b, 0, y, SCR_W, 2, RGB(120, 86, 60));
    gfx_vgrad(b, 0, y + 2, SCR_W, SCR_H - y - 2, RGB(58, 42, 32), RGB(30, 22, 16));

    static const char *const en[3] = { "HEALTH", "AMMO", "ARMOR" };
    static const char *const ru[3] = { "ЗДОРОВЬЕ", "ПАТРОНЫ", "БРОНЯ" };
    const char *const *names = TRA(en, ru);
    int vals[3];
    vals[0] = health; vals[1] = ammo; vals[2] = armor;
    px cols[3] = { health > 30 ? RGB(240, 230, 220) : RGB(250, 60, 40),
                   RGB(240, 210, 90), RGB(120, 200, 250) };
    for (int i = 0; i < 3; i++) {
        int cx = 40 + i * 80;
        gfx_text_c(b, cx, y + 16, &font_s, RGB(150, 120, 96), names[i]);
        fx_fmt(s, sizeof s, "%d", vals[i]);
        gfx_text_c(b, cx, y + 42, &font_l, cols[i], s);
        if (i) gfx_fill(b, cx - 40, y + 6, 1, SCR_H - y - 10, RGB(88, 64, 46));
    }
    uint32_t sec = (now_ms() - started) / 1000;
    fx_fmt(s, sizeof s, "%d:%02d", (int)sec / 60, (int)sec % 60);
    gfx_text(b, 4, y - 6, &font_s, RGB(200, 190, 180), s);
}

static void overlay(const band *b)
{
    int cx = SCR_W / 2, cy = VIEW_H / 2;
    gfx_fill(b, cx - 5, cy, 3, 1, RGB(200, 200, 200));      /* прицел */
    gfx_fill(b, cx + 3, cy, 3, 1, RGB(200, 200, 200));
    gfx_fill(b, cx, cy - 5, 1, 3, RGB(200, 200, 200));
    gfx_fill(b, cx, cy + 3, 1, 3, RGB(200, 200, 200));
    /* пистолет: A покой, B..D выстрел, плюс кадр вспышки */
    int shooting = (int)now_ms() < flash_until;
    int bob = shooting ? 0 : (int)((now_ms() / 120) % 4);
    dm_weapon(shooting ? 1 + (int)(((flash_until - (int)now_ms()) / 30) % 3) : 0, bob);
    if (shooting) dm_weapon(4, 0);
    if ((int)now_ms() < hurt_until) {                       /* получили по морде */
        gfx_fill(b, 0, 0, SCR_W, 3, RGB(190, 30, 20));
        gfx_fill(b, 0, VIEW_H - 3, SCR_W, 3, RGB(190, 30, 20));
        gfx_fill(b, 0, 0, 3, VIEW_H, RGB(190, 30, 20));
        gfx_fill(b, SCR_W - 3, 0, 3, VIEW_H, RGB(190, 30, 20));
    }
    if (dead) game_banner(b, TR("You died", "Ты погиб"), TR("▶ again", "▶ заново"), RGB(220, 40, 30));
    else if (level_done) game_banner(b, TR("Level complete", "Уровень пройден"), TR("▶ next", "▶ дальше"), RGB(90, 230, 120));
}

void run_doom(void)
{
    if (!dm_open(&g)) {
        uint32_t last = now_ms();
        while (!game_quit()) {
            in_poll();
            fb_begin();
            for (band *b; (b = fb_next()); ) {
                gfx_clear(b, RGB(20, 0, 0));
                gfx_text_c(b, 120, 120, &font_m, WHITE, TR("no DOOM data", "нет данных DOOM"));
            }
            game_frame_wait(&last, 33);
        }
        return;
    }
    cur_map = 0;
    mobjs = 0; mobj_cap = 0;         /* арену только что сбросило меню */
    health = 100; armor = 0; ammo = 50; dead = 0; level_done = 0;
    memset(movers, 0, sizeof movers);
    game_exit_button(BTN_DOWN);      /* низ — выстрел, а удержание 2,5 с — выход */
    game_exit_hold(2500);
    dm_load(&g, &m, cur_map);
    if (!dm_bind(&g, &m)) return;
    if (!dm_render_init(g.skyflat, g.skytex)) return;   /* не хватило ОЗУ */
    spawn();
    spawn_things();
    started = now_ms();
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        uint32_t h = in_held();
        if (dead || level_done) {
            if (in_hit() & B_RIGHT) {
                if (level_done && cur_map + 1 < g.nmaps) { cur_map++; dm_load(&g, &m, cur_map); dm_bind(&g, &m); }
                health = 100; armor = 0; ammo = 50; dead = 0; level_done = 0;
                memset(movers, 0, sizeof movers);
                spawn(); spawn_things(); started = now_ms();
            }
        } else {
            if (h & B_LEFT) dv.ang = (dv.ang + 70) & 4095;
            if (h & B_RIGHT) dv.ang = (dv.ang - 70) & 4095;
            if (in_hit() & B_DOWN) fire();
            if (h & B_UP) {
                int sp = 3600;
                int nx = dv.x + (int)(((int64_t)fx_sin_hi(dv.ang + 1024) * sp) >> 14);
                int ny = dv.y + (int)(((int64_t)fx_sin_hi(dv.ang) * sp) >> 14);
                if (!try_move(nx, dv.y)) { }
                if (!try_move(dv.x, ny)) { }
                pickups();
            }
            run_movers();
            think_enemies();
            int ss = dm_point_ssec(dv.x, dv.y);            /* лифты возят игрока */
            dv.z = (dm_fh[dm_sec_of_ssec(ss)] << 8) + EYE;
        }
        /* панель состояния меняется редко — гоним по SPI только 3D-вид */
        static int hud_state, hud_dirty;
        int st = health * 7919 + ammo * 131 + armor * 17 + (int)((now_ms() - started) / 1000);
        if (st != hud_state) { hud_state = st; hud_dirty = 1; }
        fb_begin();
        if (!hud_dirty) fb_rows(0, VIEW_H);
        for (band *b; (b = fb_next()); ) {
            dm_render(b);
            draw_things();
            overlay(b);
            if (hud_dirty) hud(b);
        }
        hud_dirty = 0;
        game_frame_wait(&last, 33);
    }
    snd_music_stop();
}
