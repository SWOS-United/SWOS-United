/*
 * mod_display2020.c -- SWOS 2020's match display, as a mod (bin/mods/display2020; Playaveli's,
 * from the discord-hooks branch, made a mod 2026-11-07). Three things, as SWOS 2020 draws them:
 *
 *   THE SCOREBOARD at the top left (its showTeamIcons and the clock and score of its UpdateTime
 *   hook, src/video/render.cpp, src/game.cpp; scores mode 4): a 72x8 darkened box at (8, 6), the
 *   minute right-aligned short of 30, the kits (its uniform0-3 by shirt type, from each side's
 *   in-match palette indices) at 31 and 71, the score centred near 57, in the game's small white
 *   font -- from the kick-off to the final whistle; not in an instant replay, the statistics or
 *   training. The game's own clock is left out for the whole match, and the name of the player on
 *   the ball goes to the top right, on the board's row.
 *
 *   THE RESULT'S BAND (half time, full time, a goal) darkened smoothly across the width instead of
 *   the Amiga's checker baked into its grid sprites,
 *
 *   ... WITH EACH SIDE'S KIT beside the score, twice the board's size, the team names 14 columns
 *   further out (its SR_TEAM_GAP_X, SR_UNIF_GAP_*); narrower than 400 columns, its Amiga/DOS
 *   layout (the kits on a row above the names, the band 7 rows taller).
 *
 * How: the engine's hooks (include/mod_api.h). spriteDraw leaves the clock and the grid sprites
 * out of the picture, notes the band's rows (and the frame buffer's pixels there before the
 * result's text) and moves the names; frameDrawn draws the board, the band's darkening and the
 * kits into an RGBA layer the picture's size, over the finished picture through gfx.h -- either
 * presenter, the GPU one or SDL's. Halving a colour is black at half alpha over it; the band's
 * text (what changed in the frame buffer since the snapshot, or what the HUD layer holds with
 * the sub-pixel scroll on) is left clear, so it stays bright. Drawing only: no sprite, no match
 * state changes (the sprites, the clock, the names are as the game made them).
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_types.h"
#include "display.h"            /* _screen_width, _screen_height, HUD_WIDTH, FRONTBUFFER_ROW_STRIDE_BYTES */
#include "dos_mode.h"           /* _gDosView */
#include "gfx.h"
#include "platform_sdl.h"       /* Platform_FrameShown, Platform_HudLayerBuffer */

#define SLUG "display2020"

extern TeamGame  _topTeamInGame, _bottomTeamInGame;
extern uint16_t  _statsTeam1Goals, _statsTeam2Goals;
extern uint16_t  _gameState, _trainingGame, _replayState, _showingUserStats, _statsTimer;
extern uint16_t  _playGame;
extern uint8_t   _gameTime[];
extern uint8_t   _smallCharsTable[];
extern void     *_spriteGraphicsPtr;
extern volatile uintptr_t _frontbufferBasePtr;
extern uint16_t  _currentPlayerIndicatorPosition;
extern uint8_t   _plNamesLengthTableCopy[];
extern uint8_t   _plOrdinalsTable[];
extern Sprite    _currentTimeSprite, _currentPlayerNameSprite, _team1NameSprite, _team2NameSprite;
extern Sprite    _gridUpperRowLeftSprite, _gridUpperRowRightSprite, _gridMiddleRowLeftSprite,
                 _gridMiddleRowRightSprite, _gridBottomRowLeftSprite, _gridBottomRowRightSprite,
                 _gridUpperRowExtraSprite, _gridMiddleRowExtraSprite, _gridBottomRowExtraSprite;
extern void      Platform_PaletteRgb(int index, uint8_t *r, uint8_t *g, uint8_t *b);
extern uint64_t  Platform_NowMs(void);

enum { BOX_X = 8, BOX_Y = 6, BOX_W = 72, BOX_H = 8 };
#define SPR_TEAM1_PLAYER_NAMES_START 0x8D       /* (game_hud.c's) */
#define SPR_TEAM2_PLAYER_NAMES_START 0x9D

/* When the match last drew its sprites. `_playGame` is match state and stays set when a live match
 * ends early and goes back to the lobby, so the board and the band are drawn only while the match
 * is still drawing (or paused: frameKept) -- the HUD layer's 100 ms rule (platform_sdl.c). */
static uint64_t s_drawnMs;

static int32_t fresh(void)
{
    return s_drawnMs && Platform_NowMs() - s_drawnMs <= 100;
}

static void frame_kept(void)
{
    if (s_drawnMs)
        s_drawnMs = Platform_NowMs();
}

static int32_t hides_clock(void)
{
    return _playGame && !_trainingGame;
}

static int32_t board_showing(void)
{
    if (!_playGame || _trainingGame || _replayState || !fresh())
        return 0;
    if (_showingUserStats || _statsTimer)                       /* the statistics */
        return 0;
    /* the final whistle (EndOfGame), the DOS shoot-out's walk off, the result */
    if (_gameState == ST_GAME_ENDED || _gameState == ST_PLAYERS_GOING_TO_SHOWER ||
        _gameState == ST_RESULT_AFTER_THE_GAME)
        return 0;
    return 1;
}

/* ---- the layer: RGBA, the picture's size, drawn over it ---- */

static uint32_t   *s_layer;                     /* 0xAARRGGBB, _screen_width a row */
static int32_t     s_layerW, s_layerH;
static GfxTexture *s_tex;
static int32_t     s_texW, s_texH;
static uint8_t    *s_rgba;                      /* (the texture's bytes: R, G, B, A) */
static int32_t     s_dirty;                     /* the layer holds something this frame */

static int32_t layer_ready(void)
{
    if (s_layer && s_layerW == _screen_width && s_layerH == _screen_height)
        return 1;
    free(s_layer);
    free(s_rgba);
    s_layerW = _screen_width;
    s_layerH = _screen_height;
    s_layer = (uint32_t *)calloc((size_t)s_layerW * s_layerH, 4);
    s_rgba = (uint8_t *)malloc((size_t)s_layerW * s_layerH * 4);
    return s_layer && s_rgba;
}

static uint32_t argb_of(int32_t index)
{
    uint8_t r, g, b;
    Platform_PaletteRgb(index & 0xFF, &r, &g, &b);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void put(int32_t x, int32_t y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < s_layerW && y < s_layerH)
        s_layer[(uint32_t)y * (uint32_t)s_layerW + (uint32_t)x] = c;
}

/* The game's small font, as DrawMenuText draws it (its body colour, its shadow). */
static int32_t text_width(const char *s)
{
    const CharsTableHeader *ct = (const CharsTableHeader *)_smallCharsTable;
    const uint16_t *conv = (const uint16_t *)(ct + 1);
    SpriteGraphics **gfx = (SpriteGraphics **)_spriteGraphicsPtr;
    int32_t w = 0;
    for (; *s; s++) {
        if (*s == ' ')
            w += ct->spaceWidth;
        else
            w += gfx[conv[(uint8_t)(*s - ' ')] + ct->spriteIndexOffset]->pixWidth + ct->charSpacing;
    }
    return w > 0 ? w - ct->charSpacing : 0;
}

static void text(int32_t x, int32_t y, const char *s, uint32_t body, uint32_t shadow)
{
    const CharsTableHeader *ct = (const CharsTableHeader *)_smallCharsTable;
    const uint16_t *conv = (const uint16_t *)(ct + 1);
    SpriteGraphics **gfx = (SpriteGraphics **)_spriteGraphicsPtr;
    for (; *s; s++) {
        const SpriteGraphics *g;
        int32_t px, py, stride;
        if (*s == ' ') {
            x += ct->spaceWidth;
            continue;
        }
        g = gfx[conv[(uint8_t)(*s - ' ')] + ct->spriteIndexOffset];
        stride = g->wquads * 16;
        for (py = 0; g->sprData && py < g->nlines; py++)
            for (px = 0; px < g->pixWidth; px++) {
                const uint8_t v = g->sprData[py * stride + px];
                if (v & 2)
                    put(x + px, y + py, body);
                else if (v & 8)
                    put(x + px, y + py, shadow);
            }
        x += g->pixWidth + ct->charSpacing;
    }
}

static const char *const k_uniform[4][8] = {
    { "........", ".12221..", ".12221+.", "..222++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".21112..", ".21112+.", "..111++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".12121..", ".12121+.", "..212++.", "..333+..", "..333+..", "...+++..", "........" },
    { "........", ".11111..", ".12221+.", "..111++.", "..333+..", "..333+..", "...+++..", "........" },
};

/* SWOS 2020's showUniform: `scale` 1 (the top-left board) or 2 (the result's band), its loops
 * running 0..7*scale inclusive. */
static void kit(int32_t x, int32_t y, const TeamGame *t, int32_t scale)
{
    const char *const *art = k_uniform[(uint16_t)t->prShirtType & 3];
    for (int32_t j = 0; j <= 7 * scale; j++)
        for (int32_t i = 0; i <= 7 * scale; i++) {
            const char c = art[j / scale][i / scale];
            const int32_t col = c == '1' ? t->prShirtCol : c == '2' ? t->prStripesCol :
                                c == '3' ? t->prShortsCol : c == '+' ? 19 : -1;
            if (col >= 0)
                put(x + i, y + j, argb_of(col));
        }
}

/* ---- the scoreboard ---- */

static void draw_board(void)
{
    const int32_t minute = _gameTime[1] * 100 + _gameTime[2] * 10 + _gameTime[3];
    const uint32_t white = argb_of(2), shadow = 0xFF000000u;
    char buf[16];

    /* SWOS 2020's drawDarkenRect fills w + 1 by h + 1: black, half seen through */
    for (int32_t y = BOX_Y; y <= BOX_Y + BOX_H; y++)
        for (int32_t x = BOX_X; x <= BOX_X + BOX_W; x++)
            put(x, y, 0x80000000u);
    /* SWOS 2020's printStringEx measures a string one pixel wider per character than it draws it
     * (getStringLength counts width + 1, the drawing advances width), so its right-aligned minute
     * ends a pixel per character short of 30 and its centred score sits half that left of 57. */
    snprintf(buf, sizeof buf, "%d'", minute);
    text(30 - (int32_t)strlen(buf) - text_width(buf), BOX_Y + 2, buf, white, shadow);
    snprintf(buf, sizeof buf, "%d-%d", (int)_statsTeam1Goals, (int)_statsTeam2Goals);
    text((114 - (int32_t)strlen(buf) - text_width(buf)) / 2, BOX_Y + 2, buf, white, shadow);
    kit(BOX_X + 23, BOX_Y + 1, &_topTeamInGame, 1);         /* (team 1, the top team) */
    kit(BOX_X + 63, BOX_Y + 1, &_bottomTeamInGame, 1);
}

/* While the board shows, the name of the player on the ball is drawn where SWOS 2020 puts it: the
 * top right, on the board's row (its text 8 rows down), right-aligned 10 columns in from the edge.
 * The top-left corner (the game's default, 4) and the top-right one (3) swap places, so the
 * indicator key still visits all four corners: 3 is then the top left, under the board (+7). */
static void name_shift(const Sprite *spr, int16_t *dx, int16_t *dy)
{
    const int16_t leftX = _gDosView ? 12 : 20, leftY = _gDosView ? 0 : 14;
    const int16_t rightY = _gDosView ? 8 : 14;
    int16_t len = 0;

    *dx = *dy = 0;
    for (int16_t i = 0; i < 32; i++)
        if ((i < 16 ? SPR_TEAM1_PLAYER_NAMES_START : SPR_TEAM2_PLAYER_NAMES_START) +
            (int16_t)(int8_t)_plOrdinalsTable[i] == spr->imageIndex) {
            len = (int16_t)(int8_t)_plNamesLengthTableCopy[i];
            break;
        }
    if (_currentPlayerIndicatorPosition == 4) {
        *dx = (int16_t)(HUD_WIDTH - 10 - len - leftX);
        *dy = (int16_t)(8 - leftY);
    } else if (_currentPlayerIndicatorPosition == 3) {
        *dx = (int16_t)(leftX - (HUD_WIDTH - 10 - len));
        *dy = (int16_t)(leftY - rightY + 7);
    }
}

/* ---- the result's band ----
 * DrawQueuedSprites leaves the grid sprites out and notes their rows, with the frame buffer's
 * pixels there before the result's text; at the present the band is darkened wherever the text did
 * not draw, and the kits go over it. Two copies, one per frame buffer (the game draws into one
 * while the other is presented). */
typedef struct {
    uintptr_t fb;                   /* the frame buffer it was drawn into */
    int32_t   y0, y1;               /* its rows, y1 exclusive; y1 <= y0: none */
    int32_t   gridTop;              /* the top grid row's y (the kits are placed from it) */
    uint8_t  *snap;                 /* the frame buffer's rows before the result's text */
} Band;
static Band s_band[2];

static int32_t wide_layout(void) { return _screen_width >= 400; }

static Band *band_for(uintptr_t fb, int32_t take)
{
    int32_t i;
    for (i = 0; i < 2; i++)
        if (s_band[i].fb == fb)
            return &s_band[i];
    if (!take)
        return NULL;
    i = s_band[0].fb && !s_band[1].fb ? 1 : (s_band[0].fb ? (s_band[0].y1 > s_band[1].y1 ? 1 : 0) : 0);
    s_band[i].fb = fb;
    s_band[i].y0 = s_band[i].y1 = 0;
    return &s_band[i];
}

static int32_t is_grid(const Sprite *spr)
{
    return spr == &_gridUpperRowLeftSprite || spr == &_gridUpperRowRightSprite ||
           spr == &_gridMiddleRowLeftSprite || spr == &_gridMiddleRowRightSprite ||
           spr == &_gridBottomRowLeftSprite || spr == &_gridBottomRowRightSprite ||
           spr == &_gridUpperRowExtraSprite || spr == &_gridMiddleRowExtraSprite ||
           spr == &_gridBottomRowExtraSprite;
}

/* A grid sprite: left out, its rows noted and copied as they are before the result's text. */
static int32_t band_grid(int16_t y, int32_t h)
{
    Band *b = band_for(_frontbufferBasePtr, 1);
    int32_t y0, y1;
    if (!b->snap)
        b->snap = (uint8_t *)malloc((size_t)PITCH_VIEW_MAX_WIDTH * 288);
    if (!b->snap)
        return 0;                                   /* the game's own band, then */
    y0 = y - (wide_layout() ? 0 : 7);
    y1 = y + h;
    if (y0 < 0) y0 = 0;
    if (y1 > _screen_height) y1 = _screen_height;
    if (y1 <= y0)
        return 1;
    if (b->y1 <= b->y0 || y < b->gridTop)
        b->gridTop = y;
    for (int32_t r = y0; r < y1; r++)
        memcpy(b->snap + (size_t)r * _screen_width,
               (const uint8_t *)_frontbufferBasePtr + (size_t)r * FRONTBUFFER_ROW_STRIDE_BYTES,
               (size_t)_screen_width);
    if (b->y1 <= b->y0) {
        b->y0 = y0;
        b->y1 = y1;
    } else {
        if (y0 < b->y0) b->y0 = y0;
        if (y1 > b->y1) b->y1 = y1;
    }
    return 1;
}

/* The band of the frame shown: black at half alpha (a colour halved) on every pixel the result's
 * text did not draw -- the frame buffer's, against the snapshot, and the HUD layer's (the text is
 * there with the sub-pixel scroll on) -- then the kits. */
static int32_t draw_band(void)
{
    const uint8_t *fb8 = Platform_FrameShown();
    const uint8_t *hud = Platform_HudLayerBuffer();
    const Band *b = fresh() && fb8 ? band_for((uintptr_t)fb8, 0) : NULL;
    const int32_t centre = _screen_width / 2;
    int32_t x1, x2, y;
    if (!b || b->y1 <= b->y0)
        return 0;
    for (y = b->y0; y < b->y1 && y < s_layerH; y++) {
        const uint8_t *now = fb8 + (size_t)y * FRONTBUFFER_ROW_STRIDE_BYTES;
        const uint8_t *was = b->snap + (size_t)y * _screen_width;
        const uint8_t *h = hud ? hud + (size_t)y * _screen_width : NULL;
        uint32_t *p = s_layer + (size_t)y * s_layerW;
        for (int32_t x = 0; x < s_layerW; x++)
            if (now[x] == was[x] && !(h && h[x]))
                p[x] = 0x80000000u;
    }
    if (_replayState)
        return 1;
    if (wide_layout()) {
        y = b->gridTop + 11;
        x1 = centre - (40 + (_statsTeam1Goals >= 10 ? 5 : 0));
        /* (its additionalGap carries over: a two-digit home score moves the away kit 5 too) */
        x2 = centre + (22 + (_statsTeam2Goals >= 10 ? 6 : _statsTeam1Goals >= 10 ? 5 : 0));
    } else {
        y = b->gridTop - 7 + 3;
        x1 = centre - 76;
        x2 = centre + 64;
    }
    kit(x1, y, &_topTeamInGame, 2);
    kit(x2, y, &_bottomTeamInGame, 2);
    return 1;
}

/* ---- the hooks ---- */

static void sprites_begin(void)
{
    Band *b = band_for(_frontbufferBasePtr, 1);
    b->y0 = b->y1 = 0;
    s_drawnMs = _playGame ? Platform_NowMs() : 0;
}

static int32_t sprite_draw(const Sprite *spr, const SpriteGraphics *gfx, int16_t *x, int16_t *y)
{
    if (!_playGame)
        return 0;
    if (spr == &_currentTimeSprite && hides_clock())
        return 1;
    if (spr == &_team1NameSprite || spr == &_team2NameSprite) {
        if (wide_layout())
            *x += spr == &_team1NameSprite ? -14 : 14;
        return 0;
    }
    if (gfx && is_grid(spr))
        return band_grid(*y, gfx->nlines);
    if (spr == &_currentPlayerNameSprite && board_showing()) {
        int16_t dx, dy;
        name_shift(spr, &dx, &dy);
        *x = (int16_t)(*x + dx);
        *y = (int16_t)(*y + dy);
    }
    return 0;
}

static void frame_drawn(void)
{
    if (!_playGame || !layer_ready())
        return;
    if (s_dirty) {
        memset(s_layer, 0, (size_t)s_layerW * s_layerH * 4);
        s_dirty = 0;
    }
    if (board_showing()) {
        draw_board();
        s_dirty = 1;
    }
    s_dirty |= draw_band();
    if (!s_dirty)
        return;
    if (!s_tex || s_texW != s_layerW || s_texH != s_layerH) {
        if (s_tex)
            Gfx_DestroyTexture(s_tex);
        s_tex = Gfx_CreateTexture(s_layerW, s_layerH, 0);
        s_texW = s_layerW;
        s_texH = s_layerH;
        if (!s_tex)
            return;
    }
    for (int32_t i = 0; i < s_layerW * s_layerH; i++) {     /* ARGB -> R, G, B, A bytes */
        const uint32_t v = s_layer[i];
        s_rgba[i * 4 + 0] = (uint8_t)(v >> 16);
        s_rgba[i * 4 + 1] = (uint8_t)(v >> 8);
        s_rgba[i * 4 + 2] = (uint8_t)v;
        s_rgba[i * 4 + 3] = (uint8_t)(v >> 24);
    }
    Gfx_UpdateTexture(s_tex, s_rgba, s_layerW * 4);
    {
        const GfxSpace space = Gfx_Space();
        const GfxBlend blend = Gfx_Blend();
        Gfx_SetSpace(GFX_SPACE_PICTURE);
        Gfx_SetBlend(GFX_BLEND_ALPHA);
        Gfx_DrawTexture(s_tex, NULL, NULL);
        Gfx_SetBlend(blend);
        Gfx_SetSpace(space);
    }
}

const ModDef ModDef_display2020 = {
    .slug = SLUG,
    .frameDrawn = frame_drawn,
    .spritesBegin = sprites_begin,
    .spriteDraw = sprite_draw,
    .frameKept = frame_kept,
};

#endif /* SWOS_SDL_BUILD */
