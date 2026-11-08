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
    if (!s_enabled || !_playGame || _trainingGame || _replayState)
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

static void kit(uint32_t *argb, int32_t pitch, int32_t x, int32_t y, const TeamGame *t)
{
    const char *const *art = k_uniform[(uint16_t)t->prShirtType & 3];
    int32_t i, j;
    for (j = 0; j < 8; j++)
        for (i = 0; i < 8; i++) {
            const char c = art[j][i];
            const int32_t col = c == '1' ? t->prShirtCol : c == '2' ? t->prStripesCol :
                                c == '3' ? t->prShortsCol : c == '+' ? 19 : -1;
            if (col >= 0)
                put(argb, pitch, x + i, y + j, argb_of(col));
        }
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

#endif
