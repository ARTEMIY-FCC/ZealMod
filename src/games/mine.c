/* mine.c — «Майнкрафт» на таймере: мир из кубиков, который можно копать и
 * застраивать.
 *
 * Памяти под мир — восемь килобайт, поэтому мир маленький (32x32x16 блоков по
 * полбайта) и замкнут в кольцо: уйдёшь на восток — придёшь с запада.
 *
 * Рисуем по столбцам, как старые движки.  На столбец пускается луч по сетке
 * (DDA), и в каждой пройденной клетке смотрим её колонку из 16 блоков:
 *  — боковая грань стоит на расстоянии входа в клетку: одно деление на клетку,
 *    дальше текстура тянется сложениями;
 *  — верхняя и нижняя грань лежат между входом и выходом, там координата
 *    текстуры берётся обратным лучом на пиксель — как пол в DOOM.
 * Клетки идут спереди назад, закрашенные строки помечаются в cover[]: когда
 * столбец закрыт целиком, дальние клетки уже не считаются.
 *
 * Управление (кнопки приходят по одной, аккорды прошивка не отдаёт):
 *   ▲ идти вперёд, ступенька в один блок берётся сама
 *   ◀ ▶ поворот
 *   ▼ коротко — копнуть блок под прицелом
 *   ▼ подержать — поставить блок из руки (подсказка внизу говорит, что будет)
 *   ▼ держать 2,5 с — выход
 */
#include "game.h"
#include "snd.h"

/* ---- размеры ------------------------------------------------------------ */
#define WZ       16               /* высота мира в блоках */
#define ONE      256              /* блок = 256 единиц (Q8) */
#define VH       204              /* высота 3D-вида, ниже — пояс */
#define CX       120
#define FOCAL    150              /* поле зрения около 77 градусов */
#define HOR      (VH / 2)
#define MAXSTEP  22               /* дальше этого числа клеток луч не идёт */
#define NSHADE   8
#define TS       8                /* текстуры 8x8 */
#define SEA      6                /* уровень воды */
#define REACH    44               /* длина луча прицела в 1/8 блока */

#define EYE      (ONE * 13 / 8)   /* глаза на 1,625 блока над ногами */
#define PHEAD    (ONE * 7 / 4)    /* рост 1,75 блока */
#define PRAD     (ONE * 5 / 16)   /* полуширина 0,31 блока */
#define VSLOPE   64               /* прицел смотрит вниз на 64/256 ≈ 14 градусов */
#define PUT_MS   400              /* дольше этого удержание «низа» = поставить */

enum { B_AIR, B_GRASS, B_DIRT, B_STONE, B_COBBLE, B_SAND, B_LOG, B_LEAF,
       B_PLANK, B_WATER, B_COAL, B_IRON, B_GOLD, B_BRICK, B_GLOW, B_BEDROCK };

enum { T_GRASS_TOP, T_GRASS_SIDE, T_DIRT, T_STONE, T_COBBLE, T_SAND,
       T_LOG_SIDE, T_LOG_TOP, T_LEAF, T_PLANK, T_WATER, T_COAL, T_IRON,
       T_GOLD, T_BRICK, T_GLOW, T_BEDROCK, T_N };

static const uint8_t blk_tex[16][3] = {   /* бок, верх, низ */
    { 0, 0, 0 },
    { T_GRASS_SIDE, T_GRASS_TOP, T_DIRT }, { T_DIRT, T_DIRT, T_DIRT },
    { T_STONE, T_STONE, T_STONE },         { T_COBBLE, T_COBBLE, T_COBBLE },
    { T_SAND, T_SAND, T_SAND },            { T_LOG_SIDE, T_LOG_TOP, T_LOG_TOP },
    { T_LEAF, T_LEAF, T_LEAF },            { T_PLANK, T_PLANK, T_PLANK },
    { T_WATER, T_WATER, T_WATER },         { T_COAL, T_COAL, T_COAL },
    { T_IRON, T_IRON, T_IRON },            { T_GOLD, T_GOLD, T_GOLD },
    { T_BRICK, T_BRICK, T_BRICK },         { T_GLOW, T_GLOW, T_GLOW },
    { T_BEDROCK, T_BEDROCK, T_BEDROCK },
};

static const char *blk_name(int i)
{
    static const char *const en[16] = {
        "", "grass", "dirt", "stone", "cobble", "sand", "log", "leaves",
        "planks", "water", "coal", "iron", "gold", "brick", "glowstone", "bedrock"
    };
    static const char *const ru[16] = {
        "", "трава", "земля", "камень", "булыжник", "песок", "бревно", "листва",
        "доски", "вода", "уголь", "железо", "золото", "кирпич", "светокамень", "бедрок"
    };
    return TRA(en, ru)[i & 15];
}

/* Палитра: по четыре оттенка на материал.  Текстуры хранят номер цвета, а
 * затенение и дымку даёт готовая таблица shadepal[уровень][цвет] — так на
 * пиксель приходится одна выборка вместо арифметики. */
static const uint8_t pal_rgb[64][3] = {
    {  58, 110,  42 }, {  74, 132,  52 }, {  90, 150,  62 }, { 104, 166,  72 },
    {  92,  68,  46 }, { 110,  82,  56 }, { 124,  94,  64 }, { 138, 106,  74 },
    { 104, 104, 110 }, { 122, 122, 128 }, { 136, 136, 142 }, { 150, 150, 156 },
    {  88,  88,  94 }, { 110, 110, 116 }, { 130, 130, 136 }, { 146, 146, 152 },
    { 206, 192, 140 }, { 220, 208, 158 }, { 232, 220, 172 }, { 240, 232, 192 },
    {  74,  56,  34 }, {  92,  70,  44 }, { 108,  84,  54 }, { 122,  96,  62 },
    { 162, 130,  84 }, { 178, 146,  98 }, { 192, 160, 110 }, { 204, 174, 124 },
    {  30,  84,  34 }, {  42, 104,  44 }, {  54, 124,  54 }, {  68, 142,  64 },
    { 150, 112,  68 }, { 168, 128,  80 }, { 182, 142,  92 }, { 196, 156, 104 },
    {  36,  80, 166 }, {  46,  98, 188 }, {  58, 116, 208 }, {  74, 134, 224 },
    { 104, 104, 110 }, {  60,  60,  64 }, {  34,  34,  36 }, {  20,  20,  22 },
    { 104, 104, 110 }, { 190, 170, 150 }, { 214, 196, 176 }, { 232, 216, 198 },
    { 104, 104, 110 }, { 214, 180,  60 }, { 236, 206,  84 }, { 250, 226, 120 },
    { 140,  58,  46 }, { 162,  72,  58 }, { 180,  86,  70 }, { 120,  48,  38 },
    { 200, 168,  80 }, { 226, 196, 110 }, { 244, 222, 150 }, { 255, 244, 190 },
    {  30,  30,  34 }, {  46,  46,  50 }, {  62,  62,  66 }, {  24,  24,  26 },
};
#define GRP(n) ((n) * 4)          /* первый цвет группы материала */

/* ---- состояние ---------------------------------------------------------- */
static uint8_t *world;            /* по полбайта на блок */
static int wn, wmask;
static uint8_t *tex;              /* T_N текстур по TS*TS байт */
static px *shadepal;              /* NSHADE * 64 */
static px *sky;                   /* готовый столбик неба и дымки, VH */
static uint8_t *cover;            /* закрашенные строки текущего столбца */
static int32_t *ydist;            /* FOCAL*256/(y-HOR) — для верхних граней */
static int npaint;                /* сколько строк столбца уже закрыто */

static int wx_, wy_, wz_;         /* ноги игрока, Q8 */
static int vz, ang, on_ground, in_water;
static int dig_x, dig_y, dig_z, dig_ok;       /* блок под прицелом */
static int put_x, put_y, put_z, put_ok;       /* пустая клетка перед ним */
static int mark_x, mark_y, mark_z, mark_ok;   /* что подсвечиваем */
static uint16_t inv[16];
static int held, down_ms, down_on;
static uint32_t mined;

static inline int bidx(int x, int y, int z)
{
    return (((x & wmask) * wn + (y & wmask)) << 4) + z;
}

static inline int getb(int x, int y, int z)
{
    if ((unsigned)z >= WZ) return B_AIR;
    int i = bidx(x, y, z);
    uint8_t v = world[i >> 1];
    return (i & 1) ? (v >> 4) : (v & 15);
}

static inline void setb(int x, int y, int z, int b)
{
    if ((unsigned)z >= WZ) return;
    int i = bidx(x, y, z);
    uint8_t *p = &world[i >> 1];
    *p = (i & 1) ? (uint8_t)((*p & 0x0F) | (b << 4)) : (uint8_t)((*p & 0xF0) | b);
}

static inline int stops(int b) { return b != B_AIR && b != B_WATER; }

/* ---- текстуры ----------------------------------------------------------- */
static int rnd(int n) { return (int)(plat_rand() % (unsigned)n); }

static void tex_fill(int t, int grp, int lo, int hi)
{
    uint8_t *d = tex + t * TS * TS;
    for (int i = 0; i < TS * TS; i++) d[i] = (uint8_t)(grp + lo + rnd(hi - lo + 1));
}

static void make_textures(void)
{
    uint8_t *d;
    tex_fill(T_GRASS_TOP, GRP(0), 1, 3);
    tex_fill(T_DIRT,      GRP(1), 0, 3);
    tex_fill(T_STONE,     GRP(2), 1, 2);
    tex_fill(T_SAND,      GRP(4), 1, 3);
    tex_fill(T_LEAF,      GRP(7), 0, 3);
    tex_fill(T_GLOW,      GRP(14), 1, 3);
    tex_fill(T_BEDROCK,   GRP(15), 0, 3);

    d = tex + T_GRASS_SIDE * TS * TS;           /* сверху зелёная кромка */
    for (int y = 0; y < TS; y++)
        for (int x = 0; x < TS; x++) {
            int edge = y == 0 || (y == 1 && ((x * 7 + y * 3) & 3));
            d[y * TS + x] = (uint8_t)(edge ? GRP(0) + 1 + rnd(3) : GRP(1) + rnd(4));
        }
    d = tex + T_COBBLE * TS * TS;               /* камушки со швами */
    for (int y = 0; y < TS; y++)
        for (int x = 0; x < TS; x++) {
            int seam = ((x + (y / 3) * 3) % 4 == 0) || (y % 3 == 0);
            d[y * TS + x] = (uint8_t)(GRP(3) + (seam ? 0 : 1 + rnd(3)));
        }
    d = tex + T_LOG_SIDE * TS * TS;             /* вертикальная кора */
    for (int y = 0; y < TS; y++)
        for (int x = 0; x < TS; x++)
            d[y * TS + x] = (uint8_t)(GRP(5) + ((x & 1) ? 2 + rnd(2) : rnd(2)));
    d = tex + T_LOG_TOP * TS * TS;              /* годовые кольца на срезе */
    for (int y = 0; y < TS; y++)
        for (int x = 0; x < TS; x++) {
            int r = iabs(x - 3) + iabs(y - 3);
            d[y * TS + x] = (uint8_t)(GRP(6) + iclamp(3 - (r & 3), 0, 3));
        }
    d = tex + T_PLANK * TS * TS;                /* доски со стыками */
    for (int y = 0; y < TS; y++)
        for (int x = 0; x < TS; x++)
            d[y * TS + x] = (uint8_t)(GRP(8) + ((y & 3) == 0 ? 0 : 1 + rnd(3)));
    d = tex + T_WATER * TS * TS;                /* волны */
    for (int y = 0; y < TS; y++)
        for (int x = 0; x < TS; x++)
            d[y * TS + x] = (uint8_t)(GRP(9) + (((x + y * 3) >> 1) & 3));
    d = tex + T_BRICK * TS * TS;                /* кладка со сдвигом ряда */
    for (int y = 0; y < TS; y++)
        for (int x = 0; x < TS; x++) {
            int off = ((y / 4) & 1) ? 2 : 0;
            int seam = (y % 4 == 0) || ((x + off) % 4 == 0);
            d[y * TS + x] = (uint8_t)(GRP(13) + (seam ? 3 : rnd(3)));
        }
    static const uint8_t ore[3][2] = { { T_COAL, 10 }, { T_IRON, 11 }, { T_GOLD, 12 } };
    for (int k = 0; k < 3; k++) {               /* камень плюс вкрапления */
        d = tex + ore[k][0] * TS * TS;
        for (int i = 0; i < TS * TS; i++) d[i] = (uint8_t)(GRP(2) + 1 + rnd(2));
        for (int i = 0; i < 8; i++) {
            int x = rnd(TS - 1), y = rnd(TS - 1);
            for (int q = 0; q < 4; q++)
                d[(y + (q >> 1)) * TS + x + (q & 1)] = (uint8_t)(GRP(ore[k][1]) + 1 + rnd(3));
        }
    }
}

/* Дальние грани тонут в дымке цвета неба — иначе горизонт превращается в кашу
 * из мелких кубиков.  Дымка нарастает не линейно, а квадратично: вблизи её
 * почти нет, зато у края видимости она прячет и саму границу мира. */
static void make_shades(void)
{
    for (int s = 0; s < NSHADE; s++) {
        int bright = 256 - s * 14;
        int fog = s * s * 4;
        for (int i = 0; i < 64; i++)
            shadepal[s * 64 + i] = px_mix(px_pack(pal_rgb[i][0] * bright >> 8,
                                                  pal_rgb[i][1] * bright >> 8,
                                                  pal_rgb[i][2] * bright >> 8),
                                          RGB(150, 186, 226), fog);
    }
}

static void make_sky(void)
{
    for (int y = 0; y < VH; y++)
        sky[y] = y < HOR ? px_mix(RGB(74, 132, 226), RGB(168, 202, 236), y * 256 / HOR)
                         : px_mix(RGB(150, 186, 226), RGB(96, 120, 96),
                                  (y - HOR) * 256 / (VH - HOR));
}

/* ---- мир ---------------------------------------------------------------- */
static uint32_t seed;

static int lattice(int gx, int gy, int g)      /* шум в узле решётки, 0..255 */
{
    uint32_t h = (uint32_t)(gx & (g - 1)) * 374761393u
               + (uint32_t)(gy & (g - 1)) * 668265263u + seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h ^ (h >> 16)) & 255);
}

/* Двулинейная интерполяция по решётке со стороной step.  Мир замкнут в кольцо,
 * узлы берутся по модулю — шва на стыке не видно. */
static int noise(int x, int y, int step)
{
    int g = wn / step;
    int gx = x / step, gy = y / step;
    int fx = ((x % step) << 8) / step, fy = ((y % step) << 8) / step;
    int a = lattice(gx, gy, g), b = lattice(gx + 1, gy, g);
    int c = lattice(gx, gy + 1, g), e = lattice(gx + 1, gy + 1, g);
    int t = a + ((b - a) * fx >> 8);
    int u = c + ((e - c) * fx >> 8);
    return t + ((u - t) * fy >> 8);
}

static int height_at(int x, int y)
{
    int h = SEA - 1 + (noise(x, y, wn / 4) * 7 >> 8) + (noise(x, y, wn / 8) * 3 >> 8);
    return iclamp(h, 1, WZ - 5);
}

/* Деревце: ствол в три блока и небольшая крона.  Крона нарочно скромная —
 * лес из пятиклеточных шапок смыкается над головой и превращает мир в тоннель. */
static void plant_tree(int x, int y, int h)
{
    for (int i = 0; i < 3; i++) setb(x, y, h + i, B_LOG);
    for (int dz = 2; dz <= 4; dz++)
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++) {
                if (dz == 4 && (dx || dy)) continue;          /* макушка */
                if (!dx && !dy && dz < 3) continue;           /* тут ствол */
                if (getb(x + dx, y + dy, h + dz) == B_AIR)
                    setb(x + dx, y + dy, h + dz, B_LEAF);
            }
}

static void gen_world(void)
{
    for (int x = 0; x < wn; x++)
        for (int y = 0; y < wn; y++) {
            int h = height_at(x, y);
            int beach = h <= SEA + 1;
            for (int z = 0; z < h; z++) {
                int b;
                if (z == 0) b = B_BEDROCK;
                else if (z >= h - 1) b = beach ? B_SAND : B_GRASS;
                else if (z >= h - 3) b = beach ? B_SAND : B_DIRT;
                else {
                    int r = rnd(100);
                    b = r < 3 ? B_COAL : (r < 5 && z < 6) ? B_IRON
                                       : (r < 6 && z < 4) ? B_GOLD : B_STONE;
                }
                setb(x, y, z, b);
            }
            for (int z = h; z <= SEA; z++) setb(x, y, z, B_WATER);
        }
    for (int i = 0; i < wn * wn / 110; i++) {         /* редкая роща */
        int x = rnd(wn), y = rnd(wn), h = height_at(x, y);
        if (getb(x, y, h - 1) != B_GRASS || h + 5 >= WZ) continue;
        if (getb(x, y, h) != B_AIR) continue;
        plant_tree(x, y, h);
    }
}

/* ---- игрок -------------------------------------------------------------- */
static int hits(int x, int y, int z)
{
    for (int i = 0; i < 4; i++) {
        int bx = (x + ((i & 1) ? PRAD : -PRAD)) >> 8;
        int by = (y + ((i & 2) ? PRAD : -PRAD)) >> 8;
        for (int bz = z >> 8; bz <= (z + PHEAD) >> 8; bz++)
            if (stops(getb(bx, by, bz))) return 1;
    }
    return 0;
}

static void spawn(void)
{
    for (int i = 0; i < wn * wn; i++) {
        int x = (wn / 2 + i % wn) & wmask, y = (wn / 2 + i / wn) & wmask;
        int h = height_at(x, y);
        if (h <= SEA + 1) continue;
        wx_ = (x << 8) + ONE / 2; wy_ = (y << 8) + ONE / 2;
        wz_ = h << 8; vz = 0; ang = 0;
        return;
    }
    wx_ = wy_ = ONE / 2; wz_ = (WZ - 2) << 8;
}

/* Прицел: короткий луч от глаз вперёд и чуть вниз.  Первый непустой блок —
 * тот, что копаем; последняя пустая клетка перед ним — та, куда ставим. */
static void aim(void)
{
    int c = fx_sin_hi(ang + 1024), s = fx_sin_hi(ang);
    int ex = wx_, ey = wy_, ez = wz_ + EYE;
    int lx = ex >> 8, ly = ey >> 8, lz = ez >> 8;
    dig_ok = put_ok = 0;
    for (int i = 1; i <= REACH; i++) {
        int d = i * 32;                       /* шаг в 1/8 блока */
        int px = ex + ((d * c) >> 14);
        int py = ey + ((d * s) >> 14);
        int pz = ez - d * VSLOPE / 256;
        if (pz < 0 || (pz >> 8) >= WZ) break;
        int bx = px >> 8, by = py >> 8, bz = pz >> 8;
        if (bx == lx && by == ly && bz == lz) continue;
        if (stops(getb(bx, by, bz))) {
            dig_x = bx & wmask; dig_y = by & wmask; dig_z = bz; dig_ok = 1;
            put_x = lx & wmask; put_y = ly & wmask; put_z = lz;
            put_ok = (unsigned)lz < WZ && getb(lx, ly, lz) == B_AIR;
            return;
        }
        lx = bx; ly = by; lz = bz;
    }
    put_x = lx & wmask; put_y = ly & wmask; put_z = lz;       /* луч ушёл в пустоту */
    put_ok = (unsigned)lz < WZ && getb(lx, ly, lz) == B_AIR;
}

static void pick_held(void)
{
    if (inv[held]) return;
    for (int i = 1; i < 16; i++) if (inv[i]) { held = i; return; }
    held = 0;
}

static void dig(void)
{
    if (!dig_ok) { SND(snd_click); return; }
    int b = getb(dig_x, dig_y, dig_z);
    if (b == B_BEDROCK) { SND(snd_click); return; }   /* дно мира не берётся */
    setb(dig_x, dig_y, dig_z, B_AIR);
    if (b == B_GRASS) b = B_DIRT;                     /* как в оригинале */
    if (inv[b] < 999) inv[b]++;
    held = b;
    mined++;
    hi_set(0, mined);
    SND(snd_pick);
}

static void put(void)
{
    if (!put_ok || !held || !inv[held]) { SND(snd_click); return; }
    setb(put_x, put_y, put_z, held);
    if (hits(wx_, wy_, wz_)) {                        /* себя замуровывать не дадим */
        setb(put_x, put_y, put_z, B_AIR);
        SND(snd_click);
        return;
    }
    inv[held]--;
    pick_held();
    SND(snd_door);
}

static void walk(void)
{
    int c = fx_sin_hi(ang + 1024), s = fx_sin_hi(ang);
    int sp = in_water ? 26 : 42;                      /* единиц Q8 за кадр */
    int nx = wx_ + ((c * sp) >> 14), ny = wy_ + ((s * sp) >> 14);
    if (!hits(nx, wy_, wz_)) wx_ = nx;
    else if (on_ground && !hits(nx, wy_, wz_ + ONE)) { wx_ = nx; wz_ = ((wz_ >> 8) + 1) << 8; }
    if (!hits(wx_, ny, wz_)) wy_ = ny;
    else if (on_ground && !hits(wx_, ny, wz_ + ONE)) { wy_ = ny; wz_ = ((wz_ >> 8) + 1) << 8; }
    wx_ &= (wn << 8) - 1;                             /* мир замкнут в кольцо */
    wy_ &= (wn << 8) - 1;
}

static void physics(void)
{
    in_water = getb(wx_ >> 8, wy_ >> 8, (wz_ + ONE / 2) >> 8) == B_WATER;
    vz -= in_water ? 1 : 5;
    if (vz < (in_water ? -12 : -90)) vz = in_water ? -12 : -90;
    int nz = wz_ + vz;
    if (nz < 0) { nz = 0; vz = 0; }
    if (hits(wx_, wy_, nz)) {
        if (vz < 0) { wz_ = ((nz >> 8) + 1) << 8; on_ground = 1; }
        vz = 0;
    } else {
        wz_ = nz;
        on_ground = 0;
    }
}

/* ---- рендер ------------------------------------------------------------- */
static int cam_z, cam_cx, cam_cy;

/* Вертикальный кусок боковой грани: текстура тянется по v с шагом vstep (Q16). */
static void span_side(const band *b, int x, int y0, int y1, const uint8_t *col,
                      int vstep, int shade, int mark)
{
    const px *sp = shadepal + shade * 64;
    int v = 0;
    if (y0 < 0) { v = vstep * -y0; y0 = 0; }
    if (y1 > VH) y1 = VH;
    int lo = y0 < b->y0 ? b->y0 : y0, hi = y1 > b->y1 ? b->y1 : y1;
    for (int y = y0; y < y1; y++, v += vstep) {
        if (cover[y]) continue;
        cover[y] = 1; npaint++;
        if (y < lo || y >= hi) continue;
        band_row(b, y)[x] = mark && (y == y0 || y == y1 - 1)
                            ? RGB(255, 255, 255) : sp[col[((v >> 16) & (TS - 1)) * TS]];
    }
}

/* Верхняя или нижняя грань: расстояние до пикселя даёт обратный луч, поэтому
 * текстура ложится с перспективой. */
static void span_flat(const band *b, int x, int y0, int y1, int hz, int texid,
                      int rdx, int rdy, int mark)
{
    const uint8_t *t = tex + texid * TS * TS;
    int dh = cam_z - hz;
    if (dh < 0) dh = -dh;
    if (y0 < 0) y0 = 0;
    if (y1 > VH) y1 = VH;
    int lo = y0 < b->y0 ? b->y0 : y0, hi = y1 > b->y1 ? b->y1 : y1;
    for (int y = y0; y < y1; y++) {
        if (cover[y]) continue;
        cover[y] = 1; npaint++;
        if (y < lo || y >= hi) continue;
        int32_t yd = ydist[y];
        if (!yd) continue;
        int d = (dh * yd) >> 8;
        if (d < 0) d = -d;
        if (d > MAXSTEP * ONE) d = MAXSTEP * ONE;
        int wx = wx_ + ((d * rdx) >> 14);
        int wy = wy_ + ((d * rdy) >> 14);
        int sh = iclamp(d >> 10, 0, NSHADE - 1);
        band_row(b, y)[x] = mark && (y == y0 || y == y1 - 1)
                ? RGB(255, 255, 255)
                : shadepal[sh * 64 + t[((wy >> 5) & (TS - 1)) * TS + ((wx >> 5) & (TS - 1))]];
    }
}

static void render_col(const band *b, int x, int c, int s)
{
    int tx = x - CX;
    int rdx = c - s * tx / FOCAL;                 /* Q14; вперёд ровно единица, */
    int rdy = s + c * tx / FOCAL;                 /* значит параметр = расстояние */

    for (int y = 0; y < VH; y++) cover[y] = 0;
    npaint = 0;

    int mx = wx_ >> 8, my = wy_ >> 8;
    int adx = iabs(rdx), ady = iabs(rdy);
    int ddx = adx > 3 ? (ONE << 14) / adx : (1 << 20);
    int ddy = ady > 3 ? (ONE << 14) / ady : (1 << 20);
    if (ddx > (1 << 20)) ddx = 1 << 20;
    if (ddy > (1 << 20)) ddy = 1 << 20;
    int stepx = rdx < 0 ? -1 : 1, stepy = rdy < 0 ? -1 : 1;
    int fx = rdx < 0 ? (wx_ & 255) : 256 - (wx_ & 255);
    int fy = rdy < 0 ? (wy_ & 255) : 256 - (wy_ & 255);
    int sdx = (fx * ddx) >> 8, sdy = (fy * ddy) >> 8;

    int dist = 0, side = -1;
    for (int step = 0; step < MAXSTEP && npaint < VH; step++) {
        int out = sdx < sdy ? sdx : sdy;              /* расстояние до выхода */
        const uint8_t *colp = world + (((mx & wmask) * wn + (my & wmask)) << 3);
        uint32_t w0 = ((const uint32_t *)colp)[0], w1 = ((const uint32_t *)colp)[1];
        if (w0 | w1) {
            int de = dist < 24 ? 24 : dist;           /* делить на ноль нельзя */
            int inv_e = (FOCAL << 12) / de;
            int bpx_e = (ONE * inv_e) >> 12;          /* высота блока на экране */
            int base_e = HOR - (((ONE - cam_z) * inv_e) >> 12);
            int vstep = bpx_e > 0 ? (TS << 16) / bpx_e : (TS << 16);
            int base_x = 0, bpx_x = 0, got_x = 0;     /* дальний край — только если нужен */
            int fog = iclamp(de >> 10, 0, NSHADE - 3);
            int inside = (mx & wmask) == cam_cx && (my & wmask) == cam_cy;
            int hit = side == 0 ? wy_ + ((de * rdy) >> 14) : wx_ + ((de * rdx) >> 14);
            int u = (hit >> 5) & (TS - 1);
            int face = side == 0 ? 1 : 2;             /* грани по осям светим по-разному */
            int in_mark = mark_ok && (mx & wmask) == mark_x && (my & wmask) == mark_y;
            int pdx = side == 0 ? stepx : 0, pdy = side == 1 ? stepy : 0;

            for (int z = 0; z < WZ; z++) {
                uint32_t w = z < 8 ? w0 : w1;
                if (!w) { z = z < 8 ? 7 : WZ; continue; }
                int blk = (int)((w >> ((z & 7) * 4)) & 15);
                if (!blk) continue;
                if (inside && cam_z >= (z << 8) && cam_z < ((z + 1) << 8)) continue;
                int mark = in_mark && z == mark_z;
                int ytop = base_e - z * bpx_e;
                int ybot = ytop + bpx_e;
                if (side >= 0 && ybot > 0 && ytop < VH) {
                    int nb = getb(mx - pdx, my - pdy, z);
                    if (nb == B_AIR || (nb == B_WATER && blk != B_WATER))
                        span_side(b, x, ytop, ybot, tex + blk_tex[blk][0] * TS * TS + u,
                                  vstep, iclamp(fog + face, 0, NSHADE - 1), mark);
                }
                /* верх блока виден, если смотрим сверху и над ним пусто */
                if (cam_z > ((z + 1) << 8) && getb(mx, my, z + 1) == B_AIR) {
                    if (!got_x) {
                        int dx_ = out < 24 ? 24 : out;
                        int inv_x = (FOCAL << 12) / dx_;
                        bpx_x = (ONE * inv_x) >> 12;
                        base_x = HOR - (((ONE - cam_z) * inv_x) >> 12);
                        got_x = 1;
                    }
                    int yn = base_e - z * bpx_e, yf = base_x - z * bpx_x;
                    if (yn > yf) span_flat(b, x, yf, yn, (z + 1) << 8, blk_tex[blk][1],
                                           rdx, rdy, mark);
                }
                /* низ блока виден снизу */
                if (z && cam_z < (z << 8) && getb(mx, my, z - 1) == B_AIR) {
                    if (!got_x) {
                        int dx_ = out < 24 ? 24 : out;
                        int inv_x = (FOCAL << 12) / dx_;
                        bpx_x = (ONE * inv_x) >> 12;
                        base_x = HOR - (((ONE - cam_z) * inv_x) >> 12);
                        got_x = 1;
                    }
                    int yn = base_e - (z - 1) * bpx_e, yf = base_x - (z - 1) * bpx_x;
                    if (yf > yn) span_flat(b, x, yn, yf, z << 8, blk_tex[blk][2],
                                           rdx, rdy, mark);
                }
            }
        }
        if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
        else           { sdy += ddy; my += stepy; side = 1; }
        dist = out;
    }
    /* всё, что осталось незакрытым, — небо и дымка у горизонта */
    int lo = b->y0, hi = b->y1 < VH ? b->y1 : VH;
    for (int y = lo; y < hi; y++) if (!cover[y]) band_row(b, y)[x] = sky[y];
}

/* ---- экран -------------------------------------------------------------- */
static void hud(const band *b)
{
    char s[32];
    int y = VH;
    gfx_fill(b, 0, y, SCR_W, SCR_H - y, RGB(38, 34, 30));
    gfx_fill(b, 0, y, SCR_W, 2, RGB(96, 88, 76));
    if (held) {                                   /* в руке — сама текстура блока */
        const uint8_t *t = tex + blk_tex[held][1] * TS * TS;
        for (int ty = 0; ty < TS; ty++)
            for (int tx = 0; tx < TS; tx++)
                gfx_fill(b, 7 + tx * 3, y + 7 + ty * 3, 3, 3, shadepal[t[ty * TS + tx]]);
        gfx_frame(b, 6, y + 6, TS * 3 + 2, TS * 3 + 2, 1, RGB(190, 182, 168));
        fx_fmt(s, sizeof s, "%s x%d", blk_name(held), inv[held]);
        gfx_text(b, 38, y + 14, &font_s, RGB(232, 226, 214), s);
    } else {
        gfx_text(b, 38, y + 14, &font_s, RGB(150, 144, 134), TR("empty hand", "рука пуста"));
    }
    const char *hint = "";
    if (down_ms > 1600) hint = TR("hold — exit", "держи — выход");
    else if (down_ms >= PUT_MS) hint = TR("release — place", "отпусти — поставить");
    else if (down_ms) hint = TR("release — dig", "отпусти — копать");
    else if (dig_ok) hint = TR("▼ dig", "▼ копать");
    if (*hint) gfx_text(b, 38, y + 27, &font_s, RGB(190, 200, 150), hint);
    else {
        fx_fmt(s, sizeof s, TR("mined %d", "добыто %d"), (int)mined);
        gfx_text(b, 38, y + 27, &font_s, RGB(160, 200, 150), s);
    }
    fx_fmt(s, sizeof s, "%d", (int)hi_get(0));
    gfx_text(b, SCR_W - 6 - gfx_text_w(&font_s, s), y + 14, &font_s, RGB(210, 190, 120), s);
}

static void crosshair(const band *b)
{
    int cy = HOR + FOCAL * VSLOPE / 256;
    px c = dig_ok ? RGB(255, 255, 255) : RGB(140, 140, 140);
    gfx_fill(b, CX - 5, cy, 4, 1, c);
    gfx_fill(b, CX + 2, cy, 4, 1, c);
    gfx_fill(b, CX, cy - 5, 1, 4, c);
    gfx_fill(b, CX, cy + 2, 1, 4, c);
}

/* ---- запуск ------------------------------------------------------------- */
static int setup(void)
{
    static const int sizes[] = { 32, 16 };
    world = 0;
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        world = mem_alloc(sizes[i] * sizes[i] * WZ / 2);
        if (world) { wn = sizes[i]; wmask = wn - 1; break; }
    }
    tex = mem_alloc(T_N * TS * TS);
    shadepal = mem_alloc(NSHADE * 64 * (int)sizeof(px));
    sky = mem_alloc(VH * (int)sizeof(px));
    cover = mem_alloc(VH);
    ydist = mem_alloc(VH * (int)sizeof(int32_t));
    if (!world || !tex || !shadepal || !sky || !cover || !ydist) return 0;
    seed = plat_rand() | 1u;
    make_textures();
    make_shades();
    make_sky();
    gen_world();
    for (int i = 0; i < 16; i++) inv[i] = 0;
    held = 0; mined = 0; on_ground = 0; vz = 0; down_ms = down_on = 0;
    spawn();
    for (int y = 0; y < VH; y++) {
        int d = y - HOR;
        ydist[y] = d ? (FOCAL << 8) / d : 0;
    }
    return 1;
}

void run_mine(void)
{
    game_exit_button(BTN_DOWN);       /* низ — действие, а удержание 2,5 с — выход */
    game_exit_hold(2500);
    uint32_t last = now_ms();
    if (!setup()) {
        while (!game_quit()) {
            in_poll();
            fb_begin();
            for (band *bb; (bb = fb_next()); ) {
                gfx_clear(bb, RGB(20, 14, 10));
                gfx_text_c(bb, 120, 120, &font_m, WHITE, TR("out of memory", "не хватило памяти"));
            }
            game_frame_wait(&last, 33);
        }
        return;
    }
    while (!game_quit()) {
        in_poll();
        uint32_t h = in_held();
        if (h & B_LEFT) ang = (ang + 56) & 4095;
        if (h & B_RIGHT) ang = (ang - 56) & 4095;
        if (h & B_UP) walk();
        physics();
        aim();
        /* «низ»: коротко копаем, подольше — ставим, совсем долго — выходим */
        if (h & B_DOWN) {
            down_on = 1;
            down_ms = (int)in_held_ms(BTN_DOWN);
        } else if (down_on) {
            down_on = 0;
            if (down_ms < PUT_MS) dig(); else put();
            down_ms = 0;
        }
        if (down_ms >= PUT_MS && put_ok) {
            mark_x = put_x; mark_y = put_y; mark_z = put_z; mark_ok = 1;
        } else {
            mark_x = dig_x; mark_y = dig_y; mark_z = dig_z; mark_ok = dig_ok;
        }
        cam_z = wz_ + EYE;
        cam_cx = (wx_ >> 8) & wmask;
        cam_cy = (wy_ >> 8) & wmask;
        int c = fx_sin_hi(ang + 1024), s = fx_sin_hi(ang);
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            for (int x = 0; x < SCR_W; x++) render_col(b, x, c, s);
            crosshair(b);
            hud(b);
        }
        game_frame_wait(&last, 33);
    }
}
