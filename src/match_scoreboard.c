/*
 * match_scoreboard.c -- SWOS 2020's scoreboard (include/match_scoreboard.h). Its showTeamIcons and
 * the clock and score of its UpdateTime hook (src/video/render.cpp, src/game.cpp), scores mode 4:
 * a 72x8 darkened box at (8, 6), the minute right-aligned short of 30, the kits (its uniform0-3 by shirt
 * type, from the in-match palette indices of each side's kit) at 31 and 71, the score centred near 57,
 * in the game's small white font.
 */
#include "match_scoreboard.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "game_types.h"
#include "display.h"            /* _screen_width, _screen_height */

extern TeamGame  _topTeamInGame, _bottomTeamInGame;
extern uint16_t  _statsTeam1Goals, _statsTeam2Goals;
extern uint16_t  _gameState, _trainingGame, _replayState, _showingUserStats, _statsTimer;
extern uint16_t  _playGame;
extern uint8_t   _gameTime[];
extern uint8_t   _smallCharsTable[];
extern void     *_spriteGraphicsPtr;
extern void      Platform_PaletteRgb(int index, uint8_t *r, uint8_t *g, uint8_t *b);

enum { BOX_X = 8, BOX_Y = 6, BOX_W = 72, BOX_H = 8 };

static int32_t s_enabled = 1;

/* When the match last drew its sprites (DrawQueuedSprites). `_playGame` is match state and stays
 * set when a live match ends early and goes back to the lobby, so the board and the band are drawn
 * only while the match is still drawing (or paused: Platform_KeepFrameFresh) -- the HUD layer's
 * 100 ms rule (platform_sdl.c). */
extern uint64_t Platform_NowMs(void);
static uint64_t s_drawnMs;

static int32_t fresh(void)
{
    return s_drawnMs && Platform_NowMs() - s_drawnMs <= 100;
}

void MatchScoreboard_KeepFresh(void)
{
    if (s_drawnMs)
        s_drawnMs = Platform_NowMs();
}

void MatchScoreboard_SetEnabled(int32_t on) { s_enabled = on != 0; }

void MatchScoreboard_Rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = BOX_X;
    *y = BOX_Y;
    *w = BOX_W + 1;             /* (SWOS 2020's drawDarkenRect fills w + 1 by h + 1) */
    *h = BOX_H + 1;
}

int32_t MatchScoreboard_HidesClock(void)
{
    return s_enabled && _playGame && !_trainingGame;
}

int32_t MatchScoreboard_Showing(void)
{
    if (!s_enabled || !_playGame || _trainingGame || _replayState || !fresh())
        return 0;
    if (_showingUserStats || _statsTimer)                       /* the statistics */
        return 0;
    /* the final whistle (EndOfGame), the DOS shoot-out's walk off, the result */
    if (_gameState == ST_GAME_ENDED || _gameState == ST_PLAYERS_GOING_TO_SHOWER ||
        _gameState == ST_RESULT_AFTER_THE_GAME)
        return 0;
    return 1;
}

/* ---- drawing ---- */

static uint32_t argb_of(int32_t index)
{
    uint8_t r, g, b;
    Platform_PaletteRgb(index & 0xFF, &r, &g, &b);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void put(uint32_t *argb, int32_t pitch, int32_t x, int32_t y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < _screen_width && y < _screen_height)
        argb[(uint32_t)y * (uint32_t)pitch + (uint32_t)x] = c;
}

static uint32_t *at(uint32_t *argb, int32_t pitch, int32_t x, int32_t y)
{
    return x >= 0 && y >= 0 && x < _screen_width && y < _screen_height
         ? &argb[(uint32_t)y * (uint32_t)pitch + (uint32_t)x] : NULL;
}

/* The game's small font, as DrawMenuText draws it (its body colour, its shadow), into ARGB. */
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

static void text(uint32_t *argb, int32_t pitch, int32_t x, int32_t y, const char *s, uint32_t body, uint32_t shadow)
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
                    put(argb, pitch, x + px, y + py, body);
                else if (v & 8)
                    put(argb, pitch, x + px, y + py, shadow);
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
static void kit_scaled(uint32_t *argb, int32_t pitch, int32_t x, int32_t y, const TeamGame *t, int32_t scale)
{
    const char *const *art = k_uniform[(uint16_t)t->prShirtType & 3];
    int32_t i, j;
    for (j = 0; j <= 7 * scale; j++)
        for (i = 0; i <= 7 * scale; i++) {
            const char c = art[j / scale][i / scale];
            const int32_t col = c == '1' ? t->prShirtCol : c == '2' ? t->prStripesCol :
                                c == '3' ? t->prShortsCol : c == '+' ? 19 : -1;
            if (col >= 0)
                put(argb, pitch, x + i, y + j, argb_of(col));
        }
}

static void kit(uint32_t *argb, int32_t pitch, int32_t x, int32_t y, const TeamGame *t)
{
    kit_scaled(argb, pitch, x, y, t, 1);
}

void MatchScoreboard_Draw(uint32_t *argb, int32_t pitch, int32_t originX, int32_t originY, int32_t overlay)
{
    const int32_t minute = _gameTime[1] * 100 + _gameTime[2] * 10 + _gameTime[3];
    const uint32_t white = argb_of(2), shadow = 0xFF000000u;
    char buf[16];
    int32_t x, y;

    /* `argb` is a picture whose (0, 0) is the screen's (originX, originY) */
    argb -= (int32_t)(originY * pitch + originX);
    for (y = BOX_Y; y <= BOX_Y + BOX_H; y++)
        for (x = BOX_X; x <= BOX_X + BOX_W; x++) {
            uint32_t *p = at(argb, pitch, x, y);
            if (!p)
                continue;
            if (overlay) {
                *p = 0x80000000u;                               /* black, half seen through */
            } else {
                const uint32_t v = *p;
                *p = 0xFF000000u | (((v >> 16 & 0xFF) / 2) << 16) | (((v >> 8 & 0xFF) / 2) << 8) | ((v & 0xFF) / 2);
            }
        }
    /* SWOS 2020's printStringEx measures a string one pixel wider per character than it draws it
     * (getStringLength counts width + 1, the drawing advances width), so its right-aligned minute
     * ends a pixel per character short of 30 and its centred score sits half that left of 57. */
    snprintf(buf, sizeof buf, "%d'", minute);
    text(argb, pitch, 30 - (int32_t)strlen(buf) - text_width(buf), BOX_Y + 2, buf, white, shadow);
    snprintf(buf, sizeof buf, "%d-%d", (int)_statsTeam1Goals, (int)_statsTeam2Goals);
    text(argb, pitch, (114 - (int32_t)strlen(buf) - text_width(buf)) / 2, BOX_Y + 2, buf, white, shadow);
    kit(argb, pitch, BOX_X + 23, BOX_Y + 1, &_topTeamInGame);        /* (team 1, the top team) */
    kit(argb, pitch, BOX_X + 63, BOX_Y + 1, &_bottomTeamInGame);
}


/* ---- the result's band: SWOS 2020's darkened banner and showTeamIconsLarge ----
 *
 * The result (half time, full time) is drawn over a band of "grid" sprites whose darkening is a
 * checker baked into the sprites (the Amiga's). SWOS 2020 darkens the band smoothly, across the
 * whole width, and puts each side's kit beside the score, twice the size of the top-left board's,
 * the team names 14 columns further out to make room (its SR_TEAM_GAP_X, SR_UNIF_GAP_*; its
 * Amiga/DOS screen modes put the kits on a row of their own above the names, the band 7 rows
 * taller). Here: DrawQueuedSprites leaves the grid sprites out and notes their rows, the picture
 * is darkened there when it is presented -- every pixel the result's text did not draw over, found
 * against a copy of the band taken before the text -- and the kits are drawn over it. Drawing only.
 * Two copies, one per frame buffer (the game draws into one while the other is presented). */
extern Sprite _gridUpperRowLeftSprite, _gridUpperRowRightSprite, _gridMiddleRowLeftSprite,
              _gridMiddleRowRightSprite, _gridBottomRowLeftSprite, _gridBottomRowRightSprite,
              _gridUpperRowExtraSprite, _gridMiddleRowExtraSprite, _gridBottomRowExtraSprite;
extern Sprite _team1NameSprite, _team2NameSprite;
extern volatile uintptr_t _frontbufferBasePtr;

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

void MatchScoreboard_SpritesBegin(void)
{
    Band *b = band_for(_frontbufferBasePtr, 1);
    b->y0 = b->y1 = 0;
    s_drawnMs = _playGame ? Platform_NowMs() : 0;
}

int32_t MatchScoreboard_Sprite(const Sprite *spr, int16_t *x, int16_t y, int32_t h)
{
    Band *b;
    int32_t y0, y1, r;
    if (!s_enabled || !_playGame)
        return 0;
    if (spr == &_team1NameSprite || spr == &_team2NameSprite) {
        if (wide_layout())
            *x += spr == &_team1NameSprite ? -14 : 14;
        return 0;
    }
    if (spr != &_gridUpperRowLeftSprite && spr != &_gridUpperRowRightSprite &&
        spr != &_gridMiddleRowLeftSprite && spr != &_gridMiddleRowRightSprite &&
        spr != &_gridBottomRowLeftSprite && spr != &_gridBottomRowRightSprite &&
        spr != &_gridUpperRowExtraSprite && spr != &_gridMiddleRowExtraSprite &&
        spr != &_gridBottomRowExtraSprite)
        return 0;
    b = band_for(_frontbufferBasePtr, 1);
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
    for (r = y0; r < y1; r++)                       /* the rows as they are before the text */
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

int32_t MatchScoreboard_BandRect(uintptr_t fb, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    const Band *b = s_enabled && fresh() ? band_for(fb, 0) : NULL;
    if (!b || b->y1 <= b->y0)
        return 0;
    *x = 0;
    *y = b->y0;
    *w = _screen_width;
    *h = b->y1 - b->y0;
    return 1;
}

void MatchScoreboard_DrawBand(uint32_t *argb, int32_t pitch, uintptr_t fb, const uint8_t *fb8)
{
    const Band *b = s_enabled ? band_for(fb, 0) : NULL;
    int32_t x, y;
    if (!b || b->y1 <= b->y0 || !fb8)
        return;
    for (y = b->y0; y < b->y1; y++) {
        const uint8_t *now = fb8 + (size_t)y * FRONTBUFFER_ROW_STRIDE_BYTES;
        const uint8_t *was = b->snap + (size_t)y * _screen_width;
        uint32_t *p = argb + (size_t)y * pitch;
        for (x = 0; x < _screen_width; x++) {
            const uint32_t v = p[x];
            if (now[x] != was[x])                   /* the result's text */
                continue;
            p[x] = 0xFF000000u | (((v >> 16 & 0xFF) / 2) << 16) | (((v >> 8 & 0xFF) / 2) << 8) | ((v & 0xFF) / 2);
        }
    }
}

void MatchScoreboard_DrawBandKits(uint32_t *argb, int32_t pitch, uintptr_t fb, int32_t originX, int32_t originY)
{
    const Band *b = s_enabled ? band_for(fb, 0) : NULL;
    const int32_t centre = _screen_width / 2;
    int32_t x1, x2, y;
    if (!b || b->y1 <= b->y0 || _replayState)
        return;
    argb -= (int32_t)(originY * pitch + originX);
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
    kit_scaled(argb, pitch, x1, y, &_topTeamInGame, 2);
    kit_scaled(argb, pitch, x2, y, &_bottomTeamInGame, 2);
}

#endif
