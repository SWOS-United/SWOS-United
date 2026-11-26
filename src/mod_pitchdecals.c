/* mod_pitchdecals.c -- the pitchdecals mod (bin/mods/pitchdecals; static): pictures a DATA mod adds
 * to the game's own pitches -- a pitch replaced whole, things drawn on the grass, the goals' fronts
 * -- each behind one of that mod's own settings. SDL only.
 *
 * A mod can carry no code (docs/MODS.md §9), so a DLC that adds to the pitch says what in its
 * mod.json, and this mod draws it:
 *
 *   "pitch_decals": [
 *     { "setting": "training_pitch", "pitches": [6],           "image": "training_pitch.png", "replace": true },
 *     { "setting": "orange_nets",    "pitches": [1,2,3,4,5,6], "image": "goal_nets.png" },
 *     { "setting": "orange_nets",    "sprite": "goal_top",     "image": "net_top.png" },
 *     { "setting": "coaching_zones", "pitches": [1,2,3,4,5],   "image": "coaching_zones.png" } ]
 *
 *   setting   the mod's own on / off setting (its "settings"), its "default" when SWOS.ini has none
 *   pitches   the pitches it is for, 1-6 (6 the training ground); none: all of them
 *   modes     "match" (a match: each team its own bench) and / or "training" (a training game:
 *             both halves at one bench); none: both
 *   image     a PNG in the mod's folder. A pitch's: the whole pitch, 672 x 880 (the world's
 *             pixels), its alpha 0 left as it was. A sprite's: the sprite as it is drawn
 *   at        [[x, y], ...]: the image is smaller than the pitch, put with its top-left at each place
 *             (a bench, once in a training game, twice in a match)
 *   replace   the image is the whole pitch: every cell from it (then the others over it)
 *   sprite    goal_top / goal_bottom: the goals' fronts (images 1091 / 1092, the net over a ball
 *             that crossed the line), drawn in their place in the match and its replays
 *
 * In the mods' order, each entry over the ones before it. Colours: each the nearest of the PC
 * game's palette with its pitch condition "normal" -- the grass and its shades onto the
 * condition's own entries, so a frozen or a wet pitch turns it as it turns the game's -- never the
 * fans' kit entries.
 *
 * ON THE GAME'S OWN PITCHES ONLY: the PC graphics (grafs_dos) or the Amiga's made from them by the
 * setup (grafs_aga/SWCPICHn.MAP: the PC palette's indices, tools/swosdata step_aga_pc); not SWOS
 * 2020's graphics, not a PC data set made from SWOS 2020, not a pitch file a mod brings, not with
 * a pitch map shown (the pitchmaps mod). Drawing only, through the engine's pitch and sprite hooks:
 * the match, a replay and a netplay peer are the same with it or without.
 *
 * SWOS_PITCHDECALS_TEST=1: the entries read, the conditions and the cells built, said in the log.
 * SWOS_PITCHDECALS_DUMP=<file.png>: the whole pitch as the game draws it (every cell, the decals in),
 * in the pitch condition's colours, written once the pitch is worked out. */
#ifdef SWOS_SDL_BUILD

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets_check.h"       /* AssetsCheck_PcFromSwos2020 */
#include "dos_mode.h"           /* _gDosGraphics, _gDosGraphicsDir */
#include "game_types.h"         /* Sprite, SpriteGraphics */
#include "jag_graphics.h"       /* JagGfx_RemapDonated */
#include "kit_palette.h"        /* KitPalette_RemapPitch */
#include "mjson.h"
#include "mod_api.h"
#include "mods.h"               /* Mods_Count, Mods_Id, Mods_Dir */
#include "swos_io.h"            /* swos_data_path, swos_data_path_base */

#include "../third_party/stb_image/stb_image.h"

#define SLUG "pitchdecals"

extern void DosGfx_BuildPalette(int pitchType, uint8_t *rgb888Out);
extern uint16_t _pitchNumberAndType;
extern uint16_t _trainingGame;
extern int _menu_draw_x_offset;
extern uint8_t *_s_pitchFullPtrs[];     /* (src/pitch.c: each cell's engine tile) */
extern int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);

enum { COLS = 42, ROWS = 55, CELLS = COLS * ROWS, TILE = 16, TILE_BYTES = 256,
       PITCH_W = COLS * TILE, PITCH_H = ROWS * TILE, MAX_DECALS = 32,
       IMG_GOAL_TOP = 1091, IMG_GOAL_BOTTOM = 1092 };

typedef struct {
    char     slug[32], setting[40];
    int32_t  def;               /* the setting's default: 1 on */
    uint8_t  pitches;           /* bit n: pitch n (1-6) */
    uint8_t  modes;             /* bit 0: a match, bit 1: a training game */
    uint8_t  replace;
    uint16_t sprite;            /* 0, or IMG_GOAL_TOP / IMG_GOAL_BOTTOM */
    int32_t  w, h;
    uint8_t *idx;               /* the image as palette indices */
    uint8_t *mask;              /* 1 where it shows */
} Decal;
enum { MAX_AT = 8 };

static Decal   s_decal[MAX_DECALS];
static int32_t s_decals;
static int32_t s_read;          /* the mods' entries read for this pitch */

/* This pitch: which, whether it is the game's own, and the cells built from the decals. */
static int32_t s_dosPitch;      /* pitchLoaded's number (PC graphics), 0 none */
static int32_t s_ready, s_on;   /* worked out / something to draw here */
static int32_t s_pitch;
static uint8_t s_cover[CELLS];  /* a decal (or the replacement) touches the cell */
static int32_t s_cells;         /* how many */
static uint8_t s_tile[CELLS][TILE_BYTES];
static const uint8_t *s_key[CELLS];     /* the engine's tile the cell was built over (1: built) */
static uint8_t s_useSprite[2];  /* goal_top / goal_bottom drawn here */
static int32_t s_spriteDecal[2];

static int32_t test_on(void) { return getenv("SWOS_PITCHDECALS_TEST") != NULL; }

/* ---- reading the mods' entries ---- */

static char *slurp(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && n < (1 << 20) && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = (char *)malloc((size_t)n + 1)) != NULL) {
        if (fread(buf, 1, (size_t)n, f) == (size_t)n) {
            buf[n] = '\0';
            *len = (int)n;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    return buf;
}

static void free_decals(void)
{
    for (int32_t i = 0; i < s_decals; i++) {
        free(s_decal[i].idx);
        free(s_decal[i].mask);
    }
    memset(s_decal, 0, sizeof s_decal);
    s_decals = 0;
}

/* The nearest palette entry to r, g, b: never the fans' kit entries (10, 11) nor maroon's (12,
 * which a purple kit takes in a match). An exact colour first, the lowest entry that has it. */
static uint8_t nearest(const uint8_t *pal, int32_t r, int32_t g, int32_t b)
{
    int32_t best = 0, bestD = 0x7FFFFFFF;
    for (int32_t i = 0; i < 256; i++) {
        if (i == 10 || i == 11 || i == 12)
            continue;
        const int32_t dr = pal[i * 3] - r, dg = pal[i * 3 + 1] - g, db = pal[i * 3 + 2] - b;
        const int32_t d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return (uint8_t)best;
}

static void load_image(Decal *d, const char *dir, const char *rel, const uint8_t *pal)
{
    char path[1400];
    int w = 0, h = 0, c = 0, far = 0;
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    uint8_t *rgba = stbi_load(path, &w, &h, &c, 4);
    if (!rgba) {
        fprintf(stderr, "[pitchdecals] %s: %s\n", path, stbi_failure_reason());
        return;
    }
    d->idx = (uint8_t *)calloc((size_t)w * h, 1);
    d->mask = (uint8_t *)calloc((size_t)w * h, 1);
    if (d->idx && d->mask) {
        for (int32_t i = 0; i < w * h; i++) {
            const uint8_t *p = rgba + (size_t)i * 4;
            if (p[3] < 128)
                continue;
            const uint8_t e = nearest(pal, p[0], p[1], p[2]);
            const int32_t dr = pal[e * 3] - p[0], dg = pal[e * 3 + 1] - p[1], db = pal[e * 3 + 2] - p[2];
            far += dr * dr + dg * dg + db * db > 3 * 8 * 8;
            d->idx[i] = e;
            d->mask[i] = 1;
        }
        d->w = w;
        d->h = h;
        if (far)
            fprintf(stderr, "[pitchdecals] %s: %d pixels of a colour the palette does not have (the nearest)\n",
                    path, far);
    }
    stbi_image_free(rgba);
}

static void read_mod(const char *slug, const char *dir, const uint8_t *pal)
{
    char path[1400];
    int len = 0;
    snprintf(path, sizeof path, "%s/mod.json", dir);
    char *json = slurp(path, &len);
    const char *arr;
    int arrl;
    if (!json)
        return;
    if (mjson_find(json, len, "$.pitch_decals", &arr, &arrl) == MJSON_TOK_ARRAY) {
        int off = 0, ko, kl, vo, vl, vt;
        while ((off = mjson_next(arr, arrl, off, &ko, &kl, &vo, &vl, &vt)) != 0 && s_decals < MAX_DECALS) {
            if (vt != MJSON_TOK_OBJECT)
                continue;
            const char *it = arr + vo;
            const int itl = vl;
            Decal *d = &s_decal[s_decals];
            char image[256] = "", sprite[32] = "";
            int rep = 0;
            memset(d, 0, sizeof *d);
            snprintf(d->slug, sizeof d->slug, "%s", slug);
            mjson_get_string(it, itl, "$.setting", d->setting, (int)sizeof d->setting);
            mjson_get_string(it, itl, "$.image", image, (int)sizeof image);
            mjson_get_string(it, itl, "$.sprite", sprite, (int)sizeof sprite);
            mjson_get_bool(it, itl, "$.replace", &rep);
            d->replace = (uint8_t)(rep != 0);
            d->sprite = strcmp(sprite, "goal_top") == 0 ? IMG_GOAL_TOP
                      : strcmp(sprite, "goal_bottom") == 0 ? IMG_GOAL_BOTTOM : 0;
            {   /* the pitches, none: all six */
                const char *pa;
                int pal2;
                if (mjson_find(it, itl, "$.pitches", &pa, &pal2) == MJSON_TOK_ARRAY) {
                    int po = 0, pko, pkl, pvo, pvl, pvt;
                    while ((po = mjson_next(pa, pal2, po, &pko, &pkl, &pvo, &pvl, &pvt)) != 0) {
                        const int n = atoi(pa + pvo);
                        if (pvt == MJSON_TOK_NUMBER && n >= 1 && n <= 6)
                            d->pitches |= (uint8_t)(1u << n);
                    }
                } else
                    d->pitches = 0x7E;
            }
            {   /* the modes, none: both */
                const char *ma;
                int mal;
                if (mjson_find(it, itl, "$.modes", &ma, &mal) == MJSON_TOK_ARRAY) {
                    int mo = 0, mko, mkl, mvo, mvl, mvt;
                    while ((mo = mjson_next(ma, mal, mo, &mko, &mkl, &mvo, &mvl, &mvt)) != 0)
                        if (mvt == MJSON_TOK_STRING)
                            d->modes |= (uint8_t)(strncmp(ma + mvo, "\"training\"", 10) == 0 ? 2
                                                  : strncmp(ma + mvo, "\"match\"", 7) == 0 ? 1 : 0);
                } else
                    d->modes = 3;
            }
            {   /* the setting's default, from the mod's own "settings" */
                const char *sa;
                int sal;
                d->def = 1;
                if (d->setting[0] && mjson_find(json, len, "$.settings", &sa, &sal) == MJSON_TOK_ARRAY) {
                    int so = 0, sko, skl, svo, svl, svt;
                    while ((so = mjson_next(sa, sal, so, &sko, &skl, &svo, &svl, &svt)) != 0) {
                        char key[40] = "", dv[16] = "";
                        mjson_get_string(sa + svo, svl, "$.key", key, (int)sizeof key);
                        if (strcmp(key, d->setting) != 0)
                            continue;
                        if (mjson_get_string(sa + svo, svl, "$.default", dv, (int)sizeof dv) > 0)
                            d->def = !(strcmp(dv, "off") == 0 || strcmp(dv, "false") == 0 || strcmp(dv, "0") == 0);
                    }
                }
            }
            if (!image[0])
                continue;
            load_image(d, dir, image, pal);
            if (!d->idx)
                continue;
            {   /* "at": the image put at each place, into a pitch-sized one */
                const char *aa;
                int aal;
                if (!d->sprite && mjson_find(it, itl, "$.at", &aa, &aal) == MJSON_TOK_ARRAY) {
                    uint8_t *bi = (uint8_t *)calloc((size_t)PITCH_W * PITCH_H, 1);
                    uint8_t *bm = (uint8_t *)calloc((size_t)PITCH_W * PITCH_H, 1);
                    int ao = 0, ako, akl, avo, avl, avt, n = 0;
                    while (bi && bm && (ao = mjson_next(aa, aal, ao, &ako, &akl, &avo, &avl, &avt)) != 0 && n < MAX_AT) {
                        double px = 0, py = 0;
                        if (avt != MJSON_TOK_ARRAY || mjson_get_number(aa + avo, avl, "$[0]", &px) == 0 ||
                            mjson_get_number(aa + avo, avl, "$[1]", &py) == 0)
                            continue;
                        n++;
                        for (int32_t y = 0; y < d->h; y++)
                            for (int32_t x = 0; x < d->w; x++) {
                                const int32_t tx = (int32_t)px + x, ty = (int32_t)py + y;
                                if (tx < 0 || ty < 0 || tx >= PITCH_W || ty >= PITCH_H || !d->mask[(size_t)y * d->w + x])
                                    continue;
                                bi[(size_t)ty * PITCH_W + tx] = d->idx[(size_t)y * d->w + x];
                                bm[(size_t)ty * PITCH_W + tx] = 1;
                            }
                    }
                    if (bi && bm) {
                        free(d->idx);
                        free(d->mask);
                        d->idx = bi;
                        d->mask = bm;
                        d->w = PITCH_W;
                        d->h = PITCH_H;
                    } else {
                        free(bi);
                        free(bm);
                    }
                }
            }
            if (!d->sprite && (d->w != PITCH_W || d->h != PITCH_H)) {
                fprintf(stderr, "[pitchdecals] %s: %s is %dx%d, a pitch's is %dx%d -- left out\n",
                        slug, image, (int)d->w, (int)d->h, PITCH_W, PITCH_H);
                free(d->idx);
                free(d->mask);
                continue;
            }
            if (test_on())
                fprintf(stderr, "[pitchdecals] %s: %s (%s%s, pitches 0x%02x, modes %d, setting %s, default %s)\n", slug, image,
                        d->sprite ? "sprite" : d->replace ? "the whole pitch" : "on the pitch",
                        d->sprite == IMG_GOAL_TOP ? " goal_top" : d->sprite == IMG_GOAL_BOTTOM ? " goal_bottom" : "",
                        d->pitches, (int)d->modes, d->setting[0] ? d->setting : "-", d->def ? "on" : "off");
            s_decals++;
        }
    }
    free(json);
}

static void read_all(void)
{
    uint8_t pal[768];
    free_decals();
    DosGfx_BuildPalette(4, pal);        /* (the condition "normal": the pictures' grass) */
    for (int32_t i = 0; i < Mods_Count(); i++)
        if (strcmp(Mods_Id(i), SLUG) != 0)
            read_mod(Mods_Id(i), Mods_Dir(i), pal);
    s_read = 1;
}

/* ---- this pitch ---- */

/* The pitch file is the game's own: found where the game's data is, not in a mod's folder. */
static int32_t own_file(const char *rel)
{
    char a[1400], b[1400];
    const char *pa = swos_data_path(rel, a, sizeof a), *pb = swos_data_path_base(rel, b, sizeof b);
    return swos_data_exists(rel) && pa && pb && strcmp(pa, pb) == 0;
}

/* Why nothing is drawn on this pitch ("" something may be). */
static const char *why_not(int32_t pitch)
{
    char rel[300];
    if (pitch < 1 || pitch > 6)
        return "no pitch 1-6";
    if (Mods_IsOn("pitchmaps") && strcmp(ModApi_Setting("pitchmaps", "map", "off"), "off") != 0)
        return "a pitch map is shown (pitchmaps)";
    if (_gDosGraphics) {
        if (strcmp(_gDosGraphicsDir, "grafs_dos") != 0)
            return "not the PC game's graphics (SWOS 2020's)";
        if (AssetsCheck_PcFromSwos2020())
            return "the PC data was made from SWOS 2020";
        snprintf(rel, sizeof rel, "grafs_dos/PITCH%d.BLK", (int)pitch);
    } else {
        if (AssetsCheck_PcFromSwos2020())
            return "the PC data was made from SWOS 2020";
        snprintf(rel, sizeof rel, "grafs_aga/SWCPICH%d.MAP", (int)pitch);
        if (!swos_data_exists(rel))
            return "the Amiga's own pitches (not made from the PC's)";
    }
    if (!own_file(rel))
        return "the pitch comes from a mod";
    return "";
}

/* The decal is for this pitch, this kind of game, and its setting is on. */
static int32_t decal_on(const Decal *d)
{
    if (!(d->pitches & (1u << s_pitch)) || !(d->modes & (_trainingGame ? 2 : 1)))
        return 0;
    return !d->setting[0] || ModApi_SettingInt(d->slug, d->setting, d->def) != 0;
}

static void work_out(void)
{
    int32_t pitch = s_dosPitch ? s_dosPitch : (int32_t)(_pitchNumberAndType >> 8) + 1;
    const char *why;
    s_ready = 1;
    s_on = 0;
    s_cells = 0;
    s_pitch = pitch;
    memset(s_cover, 0, sizeof s_cover);
    memset(s_key, 0, sizeof s_key);
    s_useSprite[0] = s_useSprite[1] = 0;
    if (!s_read)
        read_all();
    if (!s_decals)
        return;
    why = why_not(pitch);
    if (why[0]) {
        if (test_on())
            fprintf(stderr, "[pitchdecals] pitch %d: nothing drawn -- %s\n", (int)pitch, why);
        return;
    }
    for (int32_t i = 0; i < s_decals; i++) {
        const Decal *d = &s_decal[i];
        if (!decal_on(d))
            continue;
        if (d->sprite) {
            s_useSprite[d->sprite - IMG_GOAL_TOP] = 1;
            s_spriteDecal[d->sprite - IMG_GOAL_TOP] = i;
            s_on = 1;
            continue;
        }
        for (int32_t c = 0; c < CELLS; c++) {
            if (s_cover[c])
                continue;
            const int32_t x0 = (c % COLS) * TILE, y0 = (c / COLS) * TILE;
            int32_t any = d->replace;
            for (int32_t y = 0; y < TILE && !any; y++)
                for (int32_t x = 0; x < TILE && !any; x++)
                    any = d->mask[(size_t)(y0 + y) * PITCH_W + x0 + x];
            s_cover[c] = (uint8_t)(any != 0);
        }
        s_on = 1;
    }
    for (int32_t c = 0; c < CELLS; c++)
        s_cells += s_cover[c];
    if (test_on()) {
        fprintf(stderr, "[pitchdecals] pitch %d (%s, %s): %d cells, goal fronts %s/%s\n", (int)pitch,
                _gDosGraphics ? "PC graphics" : "Amiga graphics", _trainingGame ? "training" : "match", (int)s_cells,
                s_useSprite[0] ? "top" : "-", s_useSprite[1] ? "bottom" : "-");
    }
}

/* A cell's tile: the engine's (or the replacement's) with every decal on it over it. */
static const uint8_t *cell_tile(int32_t cell, const uint8_t *engineTile)
{
    if (cell < 0 || cell >= CELLS || !s_cover[cell] || !engineTile)
        return engineTile;
    if (s_key[cell] == engineTile)
        return s_tile[cell];
    {
        uint8_t *t = s_tile[cell];
        const int32_t x0 = (cell % COLS) * TILE, y0 = (cell / COLS) * TILE;
        memcpy(t, engineTile, TILE_BYTES);
        for (int32_t i = 0; i < s_decals; i++) {
            const Decal *d = &s_decal[i];
            if (d->sprite || !decal_on(d))
                continue;
            for (int32_t y = 0; y < TILE; y++)
                for (int32_t x = 0; x < TILE; x++) {
                    const size_t at = (size_t)(y0 + y) * PITCH_W + x0 + x;
                    if (d->mask[at])
                        t[y * TILE + x] = d->idx[at];
                }
        }
        /* as the engine does with its own tiles (pitch.c dos_load_pitch): off the entries the
         * Jaguar tier and the kit variants take, to their twins (the same colour) */
        JagGfx_RemapDonated(t, TILE_BYTES);
        KitPalette_RemapPitch(t, TILE_BYTES);
        s_key[cell] = engineTile;
        return t;
    }
}

/* SWOS_PITCHDECALS_DUMP: the pitch as drawn, every cell, to a PNG. */
static void dump_pitch(void)
{
    const char *path = getenv("SWOS_PITCHDECALS_DUMP");
    uint8_t pal[768];
    uint8_t *rgb;
    if (!path || !path[0] || !(rgb = (uint8_t *)malloc((size_t)PITCH_W * PITCH_H * 3)))
        return;
    DosGfx_BuildPalette((int)(_pitchNumberAndType & 0xFF), pal);
    for (int32_t c = 0; c < CELLS; c++) {
        const uint8_t *t = _s_pitchFullPtrs[c];
        const int32_t x0 = (c % COLS) * TILE, y0 = (c / COLS) * TILE;
        if (s_on && s_cells)
            t = cell_tile(c, t);
        for (int32_t y = 0; y < TILE; y++)
            for (int32_t x = 0; x < TILE; x++) {
                const uint8_t e = t ? t[y * TILE + x] : 0;
                memcpy(rgb + ((size_t)(y0 + y) * PITCH_W + x0 + x) * 3, pal + e * 3, 3);
            }
    }
    stbi_write_png(path, PITCH_W, PITCH_H, 3, rgb, PITCH_W * 3);
    fprintf(stderr, "[pitchdecals] the pitch as drawn: %s\n", path);
    free(rgb);
}

/* ---- the hooks ---- */

static void pitch_dropped(void)
{
    s_ready = 0;
    s_dosPitch = 0;
    s_read = 0;             /* (the mods and their settings may have changed since the last match) */
}

static void pitch_loaded(const char *dir, int32_t pitchNumber)
{
    (void)dir;
    s_dosPitch = pitchNumber;
    s_ready = 0;
}

static ModPitchTileFn pitch_tiles(void)
{
    if (!s_ready) {
        work_out();
        dump_pitch();
    }
    return s_on && s_cells ? cell_tile : NULL;
}

/* The goal's front `image` at the world's (wx, wy), drawn at the frame's (sx, sy): 1 when ours was
 * drawn in its place. */
static int32_t front_instead(uint16_t image, int32_t sx, int32_t sy)
{
    int32_t w = 0, h = 0, stride = 0;
    if (image != IMG_GOAL_TOP && image != IMG_GOAL_BOTTOM)
        return 0;
    if (!s_ready)
        work_out();
    if (!s_on || !s_useSprite[image - IMG_GOAL_TOP])
        return 0;
    {
        const Decal *d = &s_decal[s_spriteDecal[image - IMG_GOAL_TOP]];
        uint8_t *frame = ModApi_Frame(&stride);
        ModApi_ScreenSize(&w, &h);
        if (!frame || !d->idx)
            return 0;
        for (int32_t y = 0; y < d->h; y++) {
            const int32_t fy = sy + y;
            if (fy < 0 || fy >= h)
                continue;
            for (int32_t x = 0; x < d->w; x++) {
                const int32_t fx = sx + x;
                if (fx >= 0 && fx < w && d->mask[(size_t)y * d->w + x])
                    frame[(size_t)fy * stride + fx] = d->idx[(size_t)y * d->w + x];
            }
        }
    }
    return 1;
}

static int32_t sprite_draw(const struct Sprite *sprite, const struct SpriteGraphics *gfx, int16_t *x, int16_t *y)
{
    if (!sprite || !gfx)
        return 0;
    return front_instead((uint16_t)sprite->imageIndex, *x + _menu_draw_x_offset, *y);
}

static int32_t replay_sprite_draw(uint16_t image, int16_t left, int16_t top, int16_t sx, int16_t sy)
{
    (void)left;
    (void)top;
    return front_instead(image, sx, sy);
}

const ModDef ModDef_pitchdecals = {
    .slug             = SLUG,
    .pitchLoaded      = pitch_loaded,
    .pitchDropped     = pitch_dropped,
    .pitchTiles       = pitch_tiles,
    .spriteDraw       = sprite_draw,
    .replaySpriteDraw = replay_sprite_draw,
};

#endif /* SWOS_SDL_BUILD */
