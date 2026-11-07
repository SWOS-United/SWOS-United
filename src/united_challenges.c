/*
 * united_challenges.c -- SWOS United's challenge screens (fork mod "challenges2020"): SWOS 2020's
 * challenge mode as it looked there -- SELECT CATEGORY, a category's challenges twelve to a page
 * with their stars and descriptions, a challenge's page (PLAY SCENARIO, VIEW ONLINE STATS) and the
 * category's SCENARIO COMPLETION -- its menus imported (data_json/menus/swos2020/, the importer's
 * "challenge mode" group), their handlers here under SWOS 2020's names (game.cpp ScenarioGame).
 *
 * The challenges themselves are OpenSWOS's challenges mod's, shared: this mod draws them and asks
 * it through its "challenges" service (categories, list, info, play; OpenSWOS docs/MODS.md §18),
 * and it opens these screens in place of its own through "challenges-screens" (open). The online
 * lines and lists come from the online-records mod's service (challenges.wins / winners /
 * ranking): without it, or offline, the screens say so and the rest works.
 *
 * Not drawn yet: SWOS 2020's uniforms, red cards and injury icons on a challenge's page.
 */
#include "mod_api.h"

#if defined(SWOS_SDL_BUILD)

#include "display.h"            /* FRONTBUFFER_ROW_STRIDE_BYTES */
#include "menu_load_json.h"
#include "menu_system.h"
#include "swos2020_menus.h"
#include "text_render.h"
#include "united_scn.h"
#include "united_ui.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLUG "challenges2020"

extern void SetExitMenuFlag(void);
extern uint8_t _currentMenu[];
extern void *_charTables[];
extern volatile uintptr_t _frontbufferBasePtr;
extern int _menu_draw_x_offset;

enum { kGray = 7, kLightBrownWithYellowFrame = 6, kRed = 10, kPurple = 11, kLightBlue = 13, kGreen = 14,
       kWhiteText2 = 2, kYellowText = 15, kLeftAligned = 1 << 15, kRightAligned = 1 << 14 };

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }
static int32_t selected_ordinal(void)
{
    const MenuEntry *sel = ((Menu *)_currentMenu)->selectedEntry;
    return sel ? (int32_t)sel->ordinal : -1;
}

static void set_text(MenuEntry *e, const char *text)
{
    e->fg = (uintptr_t)text;
}
static void set_color(MenuEntry *e, uint16_t color)
{
    e->stringFlags = (uint16_t)((e->stringFlags & ~0x0F) | color);
}
static void set_back(MenuEntry *e, uint16_t color)
{
    e->background = ENTRY_FRAME_AND_BACK_COLOR;
    e->bgColor = color;
}
static int32_t text_width(const char *s, const MenuEntry *e)
{
    /* (the font the menu draws it in: menu_system.c's string entry) */
    return GetTextWidth((uint8_t *)s, (uint8_t *)_charTables[((e->stringFlags & STR_FLAGS_MASK) >> 2) / 4]);
}

/* SWOS 2020's ISS headers in full (its OnDraws) */
static const char *long_category(const char *c)
{
    return !strcmp(c, "ISS DELUXE") ? "INTERNATIONAL SUPERSTAR SOCCER DELUXE"
         : !strcmp(c, "ISS 64") ? "INTERNATIONAL SUPERSTAR SOCCER 64" : c;
}

/* An entry centred on SWOS 2020's 320 wide screen, its width the text's + pad: shift is how far the
 * loaded menu stands from the JSON's coordinates (a reference entry's x then and now). */
static void centre(MenuEntry *e, const char *text, int32_t pad, int32_t shift)
{
    const int32_t w = text_width(text, e) + pad;
    e->dwWidth = (uint16_t)w;
    e->x = (uint16_t)((320 - w) / 2 + shift);
}

/* ---- SWOS 2020's pictures (PixelmapAndStrings.h, game.cpp), its drawMapEx: '1'-'F' a palette
 * index, '0' left as it is ---- */

static const char *const k_stars[100] = {
#define S36(a) a
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000000000000000000000000000000", "0CCCCC000000000000000000000000000000",
    "00CCC8800000000000000000000000000000", "00C8C8000000000000000000000000000000",
    "000808000000000000000000000000000000", "000000000000000000000000000000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000000000000000000000000000000", "000C80000000000000000000000000000000",
    "CCCCCCC00000000000000000000000000000", "0CCCCC880000000000000000000000000000",
    "00CCC8800000000000000000000000000000", "0CCCCC000000000000000000000000000000",
    "0C888C800000000000000000000000000000", "008000800000000000000000000000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000000000000000000000000000000", "000C80000000000000000000000000000000",
    "CCCCCCC00A00000000000000000000000000", "0CCCCC8AAAAA000000000000000000000000",
    "00CCC880AAA8800000000000000000000000", "0CCCCC00A8A8000000000000000000000000",
    "0C888C800808000000000000000000000000", "008000800000000000000000000000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000A00000000000000000000000000", "000C80000A80000000000000000000000000",
    "CCCCCCAAAAAAA00000000000000000000000", "0CCCCC8AAAAA880000000000000000000000",
    "00CCC880AAA8800000000000000000000000", "0CCCCC0AAAAA000000000000000000000000",
    "0C888C8A888A800000000000000000000000", "008000808000800000000000000000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000A00000000000000000000000000", "000C80000A80000000000000000000000000",
    "CCCCCCAAAAAAA00600000000000000000000", "0CCCCC8AAAAA866666000000000000000000",
    "00CCC880AAA8806668800000000000000000", "0CCCCC0AAAAA006868000000000000000000",
    "0C888C8A888A800808000000000000000000", "008000808000800000000000000000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000A00000600000000000000000000", "000C80000A80000680000000000000000000",
    "CCCCCCAAAAAA666666600000000000000000", "0CCCCC8AAAAA866666880000000000000000",
    "00CCC880AAA8806668800000000000000000", "0CCCCC0AAAAA066666000000000000000000",
    "0C888C8A888A868886800000000000000000", "008000808000808000800000000000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000A00000600000000000000000000", "000C80000A80000680000000000000000000",
    "CCCCCCAAAAAA666666600F00000000000000", "0CCCCC8AAAAA8666668FFFFF000000000000",
    "00CCC880AAA880666880FFF8800000000000", "0CCCCC0AAAAA06666600F8F8000000000000",
    "0C888C8A888A868886800808000000000000", "008000808000808000800000000000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000A00000600000F00000000000000", "000C80000A80000680000F80000000000000",
    "CCCCCCAAAAAA666666FFFFFFF00000000000", "0CCCCC8AAAAA8666668FFFFF880000000000",
    "00CCC880AAA880666880FFF8800000000000", "0CCCCC0AAAAA0666660FFFFF000000000000",
    "0C888C8A888A8688868F888F800000000000", "008000808000808000808000800000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000A00000600000F00000000000000", "000C80000A80000680000F80000000000000",
    "CCCCCCAAAAAA666666FFFFFFF00200000000", "0CCCCC8AAAAA8666668FFFFF822222000000",
    "00CCC880AAA880666880FFF8802228800000", "0CCCCC0AAAAA0666660FFFFF002828000000",
    "0C888C8A888A8688868F888F800808000000", "008000808000808000808000800000000000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
    "000C00000A00000600000F00000200000000", "000C80000A80000680000F80000280000000",
    "CCCCCCAAAAAA666666FFFFFF222222200000", "0CCCCC8AAAAA8666668FFFFF822222880000",
    "00CCC880AAA880666880FFF8802228800000", "0CCCCC0AAAAA0666660FFFFF022222000000",
    "0C888C8A888A8688868F888F828882800000", "008000808000808000808000808000800000",
    "000000000000000000000000000000000000", "000000000000000000000000000000000000",
#undef S36
};

/* check_symbols_and_etc_12x13_4x1_map's third picture (drawCheckIcon(.., 0)) */
static const char *const k_check[13] = {
    "000000000000", "00000000E000", "0000000E2E00", "0000000E2E00", "000000E2E800", "000000E2E000",
    "00000E2E8000", "0E000E2E0000", "E2E0E2E80000", "8E2EE2E00000", "08E22E800000", "008E2E000000",
    "0008E8000000",
};
/* showLockSprite's (game.cpp): 0 on a coloured button, 1 on a grey one */
static const char *const k_lock[2][8] = {
    { "00011100", "00100010", "00100010", "0AAAAAA5", "0AAAAAA5", "0AAA4AA5", "0AAA4AA5", "0AAAAAA5" },
    { "00011100", "00100010", "00100010", "06666665", "06666665", "06664665", "06664665", "06666665" },
};

static void draw_map(const char *const *map, int32_t ox, int32_t oy, int32_t offx, int32_t offy, int32_t w,
                     int32_t h)
{
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++) {
            const char c = map[offy + y][offx + x];
            if (c != '0')
                fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + x] =
                    (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
}

static int32_t stars_width(int32_t n) { return n * 8 - 2 * (n - 1); }
static void draw_stars(int32_t n, int32_t ox, int32_t oy)
{
    if (n < 1)
        return;
    if (n > 5)
        n = 5;
    draw_map(k_stars, ox, oy, 0, 10 * (1 + 2 * (n - 1)), 36, 10);
}
void United_DrawStars(int32_t n, int32_t x, int32_t y) { draw_stars(n, x, y); }

/* ---- the challenges, from OpenSWOS's challenges mod ---- */

typedef struct { char name[32]; int32_t done, all, closed; } Cat;
typedef struct { char id[40], label[40]; int32_t difficulty, state; } Item;    /* state: 0 open, 1 done, 2 locked */

static Cat     s_cats[24];
static int32_t s_numCats, s_catPage;
static char    s_cat[32];               /* the category being shown */
static Item    s_items[200];
static int32_t s_numItems, s_page;
static char    s_sel[40];               /* the challenge on its page */
static char    s_info[4096];

static int32_t call(const char *method, const char *args, char *out, uint32_t cap)
{
    return ModApi_CallService("challenges", method, args, out, cap);
}

/* the next tab-separated field of a line; the line moves on */
static char *field(char **p)
{
    char *start = *p;
    if (!start)
        return (char *)"";
    char *tab = strpbrk(start, "\t\n");
    if (tab) {
        const char was = *tab;
        *tab = 0;
        *p = was == '\t' ? tab + 1 : NULL;
    } else {
        *p = NULL;
    }
    return start;
}

static void fetch_categories(void)
{
    static char buf[4096];
    s_numCats = 0;
    if (call("categories", "{}", buf, sizeof buf) != 0)
        return;
    for (char *line = buf; line && *line && s_numCats < 24;) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *p = line;
        Cat *c = &s_cats[s_numCats];
        snprintf(c->name, sizeof c->name, "%s", field(&p));
        c->done = atoi(field(&p));
        c->all = atoi(field(&p));
        c->closed = atoi(field(&p));
        if (strcmp(c->name, "CUSTOM") != 0)     /* (the player's own: CUSTOM CHALLENGES) */
            s_numCats++;
        line = next;
    }
}

static void fetch_list(void)
{
    static char buf[16384];
    char args[96], esc[64];
    s_numItems = 0;
    snprintf(args, sizeof args, "{\"category\":\"%s\"}", ModApi_JsonEscape(s_cat, esc, sizeof esc));
    if (call("list", args, buf, sizeof buf) != 0)
        return;
    for (char *line = buf; line && *line && s_numItems < 200;) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *p = line;
        Item *it = &s_items[s_numItems++];
        snprintf(it->id, sizeof it->id, "%s", field(&p));
        snprintf(it->label, sizeof it->label, "%s", field(&p));
        it->difficulty = atoi(field(&p));
        it->state = atoi(field(&p));
        line = next;
    }
}

static int32_t fetch_info(const char *id, char *out, uint32_t cap)
{
    char args[96], esc[64];
    snprintf(args, sizeof args, "{\"id\":\"%s\"}", ModApi_JsonEscape(id, esc, sizeof esc));
    return call("info", args, out, cap) == 0;
}

/* a field of an "info" answer ("<key>\t<value>" lines), into out */
static const char *info_get(const char *info, const char *key, char *out, uint32_t cap)
{
    const size_t k = strlen(key);
    out[0] = 0;
    for (const char *p = info; p && *p;) {
        const char *nl = strchr(p, '\n');
        if (!strncmp(p, key, k) && p[k] == '\t') {
            const size_t n = nl ? (size_t)(nl - p - k - 1) : strlen(p + k + 1);
            snprintf(out, cap, "%.*s", (int)(n < cap ? n : cap - 1), p + k + 1);
            break;
        }
        p = nl ? nl + 1 : NULL;
    }
    return out;
}
static int32_t info_num(const char *info, const char *key)
{
    char v[24];
    return atoi(info_get(info, key, v, sizeof v));
}

/* SWOS_CHALLENGES2020_TEST=<category>[/<id>] with --menu=swos2020/<a challenge screen>: the screen
 * opened straight, on that category (and challenge) -- the screens' check, as there is no way to
 * script the menus' keys. */
static void test_state(void)
{
    const char *t = getenv("SWOS_CHALLENGES2020_TEST");
    if (!t || s_cat[0])
        return;
    const char *slash = strchr(t, '/');
    snprintf(s_cat, sizeof s_cat, "%.*s", (int)(slash ? slash - t : (long)strlen(t)), t);
    if (slash)
        snprintf(s_sel, sizeof s_sel, "%s", slash + 1);
    fetch_list();
    if (s_sel[0])
        fetch_info(s_sel, s_info, sizeof s_info);
}

/* ---- SELECT CATEGORY ---- */

/* Six categories a page in SWOS 2020's buttons (act11 .. act32), MORE (act4) when there are more,
 * then - USER CREATED CONTENT - and CUSTOM CHALLENGES (actX). */
static const int32_t k_catSlots[6] = { SelectCategoryMenu_act11, SelectCategoryMenu_act12, SelectCategoryMenu_act21,
                                       SelectCategoryMenu_act22, SelectCategoryMenu_act31, SelectCategoryMenu_act32 };
static char s_catText[6][40], s_moreText[24];

static int32_t cat_shift(void) { return (int32_t)entry_at(SelectCategoryMenu_exit)->x - 112; }
static int32_t cat_shift_y(void) { return (int32_t)entry_at(SelectCategoryMenu_exit)->y - 186; }

static int32_t category_complete(const Cat *c) { return c->all && c->done == c->all; }

static void category_AfterDraw(MenuEntry *e)
{
    for (int32_t s = 0; s < 6; s++)
        if (e == entry_at(k_catSlots[s])) {
            const int32_t k = s_catPage * 6 + s;
            if (k >= s_numCats)
                return;
            const Cat *c = &s_cats[k];
            /* left of the name (SWOS 2020 padded the name with spaces for it: this font's
             * widths leave spaces out, so the icon goes by the name's own) */
            const int32_t tw = text_width((const char *)e->fg, e);
            const int32_t x = e->x + ((int32_t)e->dwWidth - tw) / 2 - 12, y = e->y + 4;
            if (category_complete(c))
                draw_map(k_check, x + 1, y - 1, 0, 0, 12, 13);
            else if (c->closed)
                draw_map(k_lock[1], x, y, 0, 0, 8, 8);
        }
}

static void selectCategoryMenu_layout(void)
{
    /* rows at 34, 58 and 82 (SWOS 2020's at 34, 64, 94 with the fifth and sixth hidden), MORE
     * under them, the user-content line and CUSTOM CHALLENGES where they were (SWOS 2020's
     * coordinates: the loaded menu stands shifted from them, as EXIT shows) */
    static const int16_t ys[6] = { 34, 34, 58, 58, 82, 82 };
    const int32_t shift = cat_shift(), shiftY = cat_shift_y();
    for (int32_t s = 0; s < 6; s++) {
        MenuEntry *e = entry_at(k_catSlots[s]);
        e->y = (uint16_t)(ys[s] + shiftY);
        e->x = (uint16_t)((s & 1 ? 162 : 2) + shift);
        e->afterDrawCb = category_AfterDraw;
    }
    MenuEntry *more = entry_at(SelectCategoryMenu_act4);
    more->x = (uint16_t)(72 + shift);
    more->y = (uint16_t)(106 + shiftY);
}

static void selectCategoryMenu_OnInit(void)
{
    fetch_categories();
    if (s_catPage * 6 >= s_numCats)
        s_catPage = 0;
    selectCategoryMenu_layout();
}

static void selectCategoryMenu_OnDraw(void)
{
    const int32_t pages = (s_numCats + 5) / 6;
    selectCategoryMenu_layout();
    for (int32_t s = 0; s < 6; s++) {
        MenuEntry *e = entry_at(k_catSlots[s]);
        const int32_t k = s_catPage * 6 + s;
        e->isInvisible = k >= s_numCats;
        if (k >= s_numCats)
            continue;
        const Cat *c = &s_cats[k];
        const int32_t done = category_complete(c);
        snprintf(s_catText[s], sizeof s_catText[s], "%s", c->name);
        set_text(e, s_catText[s]);
        set_back(e, done ? kLightBlue : c->closed ? kGray : kGreen);
    }
    MenuEntry *more = entry_at(SelectCategoryMenu_act4);
    more->isInvisible = pages <= 1;
    snprintf(s_moreText, sizeof s_moreText, "MORE (%d / %d)", (int)s_catPage + 1, (int)(pages ? pages : 1));
    set_text(more, s_moreText);
}

static void selectCategoryMenu_OnReturn(void) {}
static void selectCategoryMenu_OnBackground(void) {}

static void show(const char *name)
{
    uint8_t *menu = MenuJson_Load(name);
    if (menu)
        ShowMenu(menu);
    else
        fprintf(stderr, "[challenges2020] cannot show %s\n", name);
}

static void open_category(int32_t slot)
{
    const int32_t k = s_catPage * 6 + slot;
    if (k >= s_numCats)
        return;
    if (s_cats[k].closed) {
        ModApi_ShowError("THIS CATEGORY WILL BE AVAILABLE, AFTER RELEASING LATER.");
        return;
    }
    snprintf(s_cat, sizeof s_cat, "%s", s_cats[k].name);
    s_page = 0;
    show("swos2020/scenarioModeMainMenu");
    fetch_categories();                     /* (what was beaten since) */
}

static void cat2_onSelect(void) { open_category(0); }
static void cat3_onSelect(void) { open_category(1); }
static void cat4_onSelect(void) { open_category(2); }
static void cat5_onSelect(void) { open_category(3); }
static void cat6_onSelect(void) { open_category(4); }
static void cat7_onSelect(void) { open_category(5); }
static void cat8_onSelect(void)             /* act4: MORE */
{
    const int32_t pages = (s_numCats + 5) / 6;
    if (pages > 1)
        s_catPage = (s_catPage + 1) % pages;
}

static void catX_onSelect(void)             /* CUSTOM CHALLENGES: SWOS 2020's CREATE / SHARE / PLAY */
{
    show("swos2020/customChallengesMainMenu");
    fetch_categories();
}

static void selectCategoryMenu_exit_onSelect(void) { SetExitMenuFlag(); }

/* ---- a category's challenges: twelve a page ---- */

static const int32_t k_slots[12] = {
    ScenarioModeMainMenu_s00, ScenarioModeMainMenu_s01, ScenarioModeMainMenu_s02, ScenarioModeMainMenu_s10,
    ScenarioModeMainMenu_s11, ScenarioModeMainMenu_s12, ScenarioModeMainMenu_s20, ScenarioModeMainMenu_s21,
    ScenarioModeMainMenu_s22, ScenarioModeMainMenu_s30, ScenarioModeMainMenu_s31, ScenarioModeMainMenu_s32,
};
static char s_slotText[12][48], s_header[96], s_pageInfo[24], s_desc[5][72];
static char s_descFor[40];

static int32_t pages_of(int32_t n, int32_t per) { return n ? (n + per - 1) / per : 1; }

/* SWOS 2020's shortenNameEx: too wide for the button (limit 102), the end cut to fit */
static void shorten(char *s, const MenuEntry *e, int32_t limit)
{
    size_t n = strlen(s);
    while (n > 1 && text_width(s, e) > limit)
        s[--n] = 0;
}

static void slot_AfterDraw(MenuEntry *e)
{
    for (int32_t i = 0; i < 12; i++)
        if (e == entry_at(k_slots[i])) {
            const int32_t idx = s_page * 12 + i;
            if (idx >= s_numItems)
                return;
            const Item *it = &s_items[idx];
            const int32_t tw = text_width((const char *)e->fg, e);
            const int32_t x = e->x + ((int32_t)e->dwWidth - tw) / 2 - 11, y = e->y + 3;
            if (it->state == 1)
                draw_map(k_check, x + 1, y - 1, 0, 0, 12, 13);
            else if (it->state == 2)
                draw_map(k_lock[1], x, y, 0, 0, 8, 8);
            draw_stars(it->difficulty, e->x + ((int32_t)e->dwWidth - stars_width(it->difficulty)) / 2,
                       e->y + e->dwHeight + 1);
        }
}

static void arrow_AfterDraw(MenuEntry *e)
{
    United_DrawPageArrow(e->ordinal == ScenarioModeMainMenu_scrollDown, e->x + 2, e->y + 1);
}

static void scenarioModeMainMenu_OnInit(void)
{
    test_state();
    fetch_list();
    s_descFor[0] = 0;
    if (s_page >= pages_of(s_numItems, 12))
        s_page = 0;
}

static void show_description(int32_t idx)
{
    static char info[4096];
    char v[72];
    if (idx < 0) {          /* (no challenge under the cursor: SWOS 2020's own lines) */
        static const char *const lines[5] = { "", "BEAT CHALLENGES TO UNLOCK NEW ONES.",
                                              "PROVE YOUR SKILLS AND GO FOR IT.", "", "" };
        for (int32_t k = 0; k < 5; k++)
            snprintf(s_desc[k], sizeof s_desc[k], "%s", lines[k]);
        s_descFor[0] = 0;
        return;
    }
    if (!strcmp(s_descFor, s_items[idx].id))
        return;
    snprintf(s_descFor, sizeof s_descFor, "%s", s_items[idx].id);
    if (!fetch_info(s_items[idx].id, info, sizeof info))
        info[0] = 0;
    for (int32_t k = 0; k < 5; k++) {
        char key[16];
        snprintf(key, sizeof key, "description%d", (int)k + 1);
        snprintf(s_desc[k], sizeof s_desc[k], "%s", info_get(info, key, v, sizeof v));
    }
    if (!s_desc[0][0] && !s_desc[1][0])     /* (a challenge of OpenSWOS's own: its one line) */
        snprintf(s_desc[2], sizeof s_desc[2], "%s", info_get(info, "about", v, sizeof v));
}

static void scenarioModeMainMenu_OnDraw(void)
{
    const int32_t shift = (int32_t)entry_at(ScenarioModeMainMenu_exit)->x - 167;
    const int32_t pages = pages_of(s_numItems, 12);
    if (s_page >= pages)
        s_page = pages - 1;

    snprintf(s_header, sizeof s_header, "%s (%d)", long_category(s_cat), (int)s_numItems);
    MenuEntry *h = entry_at(ScenarioModeMainMenu_header);
    set_text(h, s_header);
    centre(h, long_category(s_cat), 36, shift);

    for (int32_t i = 0; i < 12; i++) {
        MenuEntry *e = entry_at(k_slots[i]);
        const int32_t idx = s_page * 12 + i;
        e->afterDrawCb = slot_AfterDraw;
        if (idx < s_numItems) {
            const Item *it = &s_items[idx];
            const int32_t marked = it->state != 0;
            snprintf(s_slotText[i], sizeof s_slotText[i], "%s", it->label);
            shorten(s_slotText[i], e, marked ? 86 : 102);     /* (an icon left of it: 12 + room) */
            set_back(e, it->state == 2 ? kGray : it->state == 1 ? kLightBlue : kGreen);
        } else {
            s_slotText[i][0] = 0;
            set_back(e, kGray);
        }
        set_text(e, s_slotText[i]);
    }

    {
        const int32_t sel = selected_ordinal();
        int32_t idx = -1;
        for (int32_t i = 0; i < 12; i++)
            if (sel == k_slots[i] && s_page * 12 + i < s_numItems)
                idx = s_page * 12 + i;
        show_description(idx);
        for (int32_t k = 0; k < 5; k++) {
            MenuEntry *e = entry_at(ScenarioModeMainMenu_description1 + k);
            set_text(e, s_desc[k]);
            if (k == 4)
                set_color(e, kYellowText);
        }
    }

    snprintf(s_pageInfo, sizeof s_pageInfo, "(%d / %d)", (int)s_page + 1, (int)pages);
    set_text(entry_at(ScenarioModeMainMenu_pageInfo), s_pageInfo);
    entry_at(ScenarioModeMainMenu_pageInfo)->isInvisible = pages <= 1;
    entry_at(ScenarioModeMainMenu_scrollUp)->isInvisible = pages <= 1;
    entry_at(ScenarioModeMainMenu_scrollDown)->isInvisible = pages <= 1;
    entry_at(ScenarioModeMainMenu_scrollUp)->afterDrawCb = arrow_AfterDraw;
    entry_at(ScenarioModeMainMenu_scrollDown)->afterDrawCb = arrow_AfterDraw;
    entry_at(ScenarioModeMainMenu_s02)->upEntry = (int8_t)(pages > 1 ? ScenarioModeMainMenu_scrollUp : -1);
    entry_at(ScenarioModeMainMenu_s32)->downEntry =
        (int8_t)(pages > 1 ? ScenarioModeMainMenu_scrollDown : ScenarioModeMainMenu_exit);
}

static void scenarioModeMainMenu_OnReturn(void) {}
static void scenarioModeMainMenu_OnBackground(void) {}
static void scenarioModeMainMenu_OnExit(void) { SetExitMenuFlag(); }
static void scenarioModeMain_scrollUp_onSelect(void)
{
    const int32_t pages = pages_of(s_numItems, 12);
    s_page = (s_page + pages - 1) % pages;
}
static void scenarioModeMain_scrollDown_onSelect(void)
{
    s_page = (s_page + 1) % pages_of(s_numItems, 12);
}

static void pick(int32_t slot)
{
    const int32_t idx = s_page * 12 + slot;
    if (idx >= s_numItems || s_items[idx].state == 2)
        return;
    snprintf(s_sel, sizeof s_sel, "%s", s_items[idx].id);
    show("swos2020/selectScenarioActionMenu");
    fetch_list();                           /* (its state now) */
    s_descFor[0] = 0;
}

static void s00_OnSelect(void) { pick(0); }
static void s01_OnSelect(void) { pick(1); }
static void s02_OnSelect(void) { pick(2); }
static void s10_OnSelect(void) { pick(3); }
static void s11_OnSelect(void) { pick(4); }
static void s12_OnSelect(void) { pick(5); }
static void s20_OnSelect(void) { pick(6); }
static void s21_OnSelect(void) { pick(7); }
static void s22_OnSelect(void) { pick(8); }
static void s30_OnSelect(void) { pick(9); }
static void s31_OnSelect(void) { pick(10); }
static void s32_OnSelect(void) { pick(11); }

static void scenCompletion_OnSelect(void) { show("swos2020/scenarioCompletionMenu"); }

/* ---- a challenge's page ---- */

static char s_t1[40], s_t2[40], s_score[24], s_time[32], s_start[64], s_etPen[96], s_win[48], s_beat[72],
            s_actHeader[64];
static int32_t s_actDifficulty;

/* SWOS 2020's getInitialTimeStr */
static void time_text(int32_t m, int32_t shootout, char *out, uint32_t cap)
{
    if (shootout)
        m = 120;
    if (m == 0)
        snprintf(out, cap, "MATCH START");
    else if (m == 45)
        snprintf(out, cap, "2ND HALF");
    else if (m == 90)
        snprintf(out, cap, "EXTRA TIME");
    else if (m == 120)
        snprintf(out, cap, "A.E.T.");
    else {
        const int32_t teen = (m % 100) >= 11 && (m % 100) <= 13;
        const char *suffix = teen ? "TH" : m % 10 == 1 ? "ST" : m % 10 == 2 ? "ND" : m % 10 == 3 ? "RD" : "TH";
        snprintf(out, cap, "%d%s", (int)m, suffix);
    }
}

/* the win message draws the stars under itself, as SWOS 2020's OnDraw drew them there */
static void stars_AfterDraw(MenuEntry *e)
{
    draw_stars(s_actDifficulty, e->x + ((int32_t)e->dwWidth - stars_width(s_actDifficulty)) / 2,
               e->y + e->dwHeight + 3);
}

static void selectScenarioActionMenu_OnInit(void) { test_state(); }

static void selectScenarioActionMenu_OnDraw(void)
{
    char v[96], t0[32], t1[32];
    const int32_t shift = (int32_t)entry_at(SelectScenarioActionMenu_exit)->x - 112;
    if (!fetch_info(s_sel, s_info, sizeof s_info))
        return;
    const char *info = s_info;
    const int32_t yours = info_num(info, "yours");
    info_get(info, "team0", t0, sizeof t0);
    info_get(info, "team1", t1, sizeof t1);

    info_get(info, "title", v, sizeof v);
    if (!v[0]) {
        char cat[40];
        info_get(info, "category", cat, sizeof cat);
        snprintf(v, sizeof v, "%s", long_category(cat[0] ? cat : "CHALLENGES"));
    }
    snprintf(s_actHeader, sizeof s_actHeader, "%s", v);
    MenuEntry *h = entry_at(SelectScenarioActionMenu_header);
    set_text(h, s_actHeader);
    centre(h, s_actHeader, 24, shift);

    /* the teams: yours purple, the CPU's red */
    snprintf(s_t1, sizeof s_t1, "%s ", t0);
    snprintf(s_t2, sizeof s_t2, " %s", t1);
    set_text(entry_at(SelectScenarioActionMenu_leftTeam), s_t1);
    set_text(entry_at(SelectScenarioActionMenu_rightTeam), s_t2);
    set_back(entry_at(SelectScenarioActionMenu_leftTeam), yours == 0 ? kPurple : kRed);
    set_back(entry_at(SelectScenarioActionMenu_rightTeam), yours == 1 ? kPurple : kRed);
    snprintf(s_score, sizeof s_score, "%d - %d", info_num(info, "score0"), info_num(info, "score1"));
    set_text(entry_at(SelectScenarioActionMenu_centerScore), s_score);

    const int32_t shootout = info_num(info, "shootout");
    time_text(info_num(info, "minute"), shootout, s_time, sizeof s_time);
    set_text(entry_at(SelectScenarioActionMenu_initialTime), s_time);
    entry_at(SelectScenarioActionMenu_cpuInjurySpeed1)->isInvisible = 1;
    entry_at(SelectScenarioActionMenu_cpuInjurySpeed2)->isInvisible = 1;

    /* SWOS 2020's getStartingEventStr: the event without its side, BY its team */
    {
        static const char *const kinds[6] = { "KICK OFF", "CORNER KICK", "FREE KICK", "PENALTY KICK", "THROW IN",
                                              "GOAL KICK" };
        const int32_t kind = info_num(info, "restart");
        const int32_t team = kind ? info_num(info, "restart_team") : info_num(info, "kick_off");
        snprintf(s_start, sizeof s_start, "%s BY %s", shootout ? "PENALTY SHOOTOUT" : kinds[kind >= 0 && kind < 6 ? kind : 0],
                 team == 0 ? t0 : team == 1 ? t1 : "RANDOM TEAM");
        set_text(entry_at(SelectScenarioActionMenu_startingEvent), s_start);
    }

    /* SWOS 2020's getEtPenStr: the aggregate, extra time and penalties, the subs */
    {
        char agg[32] = "", et[64], subs[24];
        const int32_t extra = info_num(info, "extra_time"), pens = info_num(info, "penalties");
        const int32_t rule = info_num(info, "extra_time_rule");
        const char *goal = rule == 1 ? " (GOLDEN GOAL)" : rule == 2 ? " (SILVER GOAL)" : "";
        if (info_num(info, "second_leg"))
            snprintf(agg, sizeof agg, "AGG. %d-%d, ", info_num(info, "score0") + info_num(info, "first_leg0"),
                     info_num(info, "score1") + info_num(info, "first_leg1"));
        if (shootout)
            snprintf(et, sizeof et, "PENALTY SHOOTOUT, MANUAL GK CONTROL");
        else if (extra)
            snprintf(et, sizeof et, "%s%s, %sPENALTY SHOOTOUT", rule ? "E.T." : "EXTRA TIME", goal, pens ? "" : "NO ");
        else
            snprintf(et, sizeof et, "NO EXTRA TIME, %sPENALTY SHOOTOUT", pens ? "" : "NO ");
        {
            const int32_t lo = info_num(info, "subs_min"), hi = info_num(info, "subs_max");
            if (!info_num(info, "subs_set") || (lo == 0 && (hi == 0 || hi == 5)))
                snprintf(subs, sizeof subs, ", NO SUBS");
            else
                snprintf(subs, sizeof subs, ", %d-%d SUBS", (int)lo, (int)hi);
        }
        snprintf(s_etPen, sizeof s_etPen, "%s%s%s", agg, et, subs);
        set_text(entry_at(SelectScenarioActionMenu_etPen), s_etPen);
    }

    info_get(info, "win_message", v, sizeof v);
    if (!v[0]) {
        const int32_t win = info_num(info, "win");
        snprintf(v, sizeof v, "%s MUST %s", yours == 0 ? t0 : t1, win == 1 ? "NOT LOSE" : "WIN");
    }
    snprintf(s_win, sizeof s_win, "%s", v);
    set_text(entry_at(SelectScenarioActionMenu_winMsg), s_win);
    set_color(entry_at(SelectScenarioActionMenu_winMsg), kYellowText);
    entry_at(SelectScenarioActionMenu_winMsg)->afterDrawCb = stars_AfterDraw;
    s_actDifficulty = info_num(info, "difficulty");

    /* how often you have beaten it: online (SWOS 2020's count), else done here or not */
    {
        char args[256], n1[96], n2[96], out[16];
        snprintf(args, sizeof args, "{\"name\":\"%s\",\"category\":\"%s\"}",
                 ModApi_JsonEscape(info_get(info, "name", v, sizeof v), n1, sizeof n1),
                 ModApi_JsonEscape(info_get(info, "category", t0, sizeof t0), n2, sizeof n2));
        const int32_t rc = ModApi_CallService("online-records", "challenges.wins", args, out, sizeof out);
        const int32_t wins = rc == 0 ? atoi(out) : -1;
        if (wins > 1)
            snprintf(s_beat, sizeof s_beat, "YOU HAVE BEATEN THIS SCENARIO %d TIMES.", (int)wins);
        else if (wins == 1 || (wins < 0 && info_num(info, "state") == 1))
            snprintf(s_beat, sizeof s_beat, "YOU HAVE BEATEN THIS SCENARIO ONCE.");
        else
            snprintf(s_beat, sizeof s_beat, "YOU HAVE NOT BEATEN THIS SCENARIO YET.");
        set_text(entry_at(SelectScenarioActionMenu_beatMsg), s_beat);
    }
}

static void act1_onSelect(void)             /* PLAY SCENARIO */
{
    char args[96], esc[64], out[32];
    snprintf(args, sizeof args, "{\"id\":\"%s\"}", ModApi_JsonEscape(s_sel, esc, sizeof esc));
    call("play", args, out, sizeof out);
}

static void act2_onSelect(void) { show("swos2020/scenarioOnlineStatsMenu"); }   /* VIEW ONLINE STATS */
static void selectScenarioActionMenu_onSelect(void) { SetExitMenuFlag(); }      /* EXIT */

/* ---- the two online tables: who has beaten a challenge, and a category's completion ---- */

enum { kRows = 15 };
static char  s_table[32768];
static int32_t s_tableRows, s_tablePage, s_tableState;  /* state: 1 waiting, 0 here, -1 none */
static char  s_cells[kRows][2][32], s_me[40], s_dataInfo[48], s_tableHeader[72], s_tablePageInfo[24];

static void table_fetch(const char *method, const char *args)
{
    s_tableState = ModApi_CallService("online-records", method, args, s_table, sizeof s_table);
    s_tableRows = 0;
    if (s_tableState == 0)
        for (const char *p = s_table; *p; p++)
            s_tableRows += *p == '\n';
}

/* row i of the answer: its two fields */
static void table_row(int32_t i, char *a, uint32_t acap, char *b, uint32_t bcap)
{
    const char *p = s_table;
    a[0] = b[0] = 0;
    for (int32_t k = 0; k < i && p; k++) {
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    if (!p || !*p)
        return;
    const char *tab = strchr(p, '\t'), *nl = strchr(p, '\n');
    if (!tab || (nl && tab > nl))
        return;
    snprintf(a, acap, "%.*s", (int)(tab - p), p);
    snprintf(b, bcap, "%.*s", (int)(nl ? nl - tab - 1 : (long)strlen(tab + 1)), tab + 1);
}

static void table_arrow_AfterDraw(MenuEntry *e)
{
    const int32_t down = e->ordinal == ScenarioOnlineStatsMenu_scrollDown || e->ordinal == ScenarioCompletionMenu_scrollDown;
    United_DrawPageArrow(down, e->x + 2, e->y + 1);
}

/* The table's draw, both menus' (the same layout: the name's column, the number's). total: the
 * percentage's base (the completion's), 0 none. */
static void table_draw(int32_t user0, int32_t value0, int32_t up, int32_t down, int32_t page, int32_t info,
                       int32_t username, const char *valueHead, int32_t total)
{
    const char *me = NULL;
    char who[40] = "";
    int32_t sum = 0;
    {
        extern const char *United_UserName(void);
        me = United_UserName();
        snprintf(who, sizeof who, "%s", me ? me : "");
        for (char *c = who; *c; c++)
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 32);
    }
    snprintf(s_me, sizeof s_me, who[0] ? "- %s -" : "", who);
    set_text(entry_at(username), s_me);
    set_back(entry_at(user0), kLightBrownWithYellowFrame);
    set_back(entry_at(value0), kLightBrownWithYellowFrame);
    set_text(entry_at(user0), "PLAYER NAME");
    set_text(entry_at(value0), valueHead);

    const int32_t pages = pages_of(s_tableRows, kRows);
    if (s_tablePage >= pages)
        s_tablePage = pages - 1;
    for (int32_t i = 0; i < kRows; i++) {
        char a[32], b[16];
        table_row(s_tablePage * kRows + i, a, sizeof a, b, sizeof b);
        snprintf(s_cells[i][0], sizeof s_cells[i][0], "%s", a);
        if (a[0] && total > 0) {
            int32_t pct = (atoi(b) * 100 + total / 2) / total;
            snprintf(s_cells[i][1], sizeof s_cells[i][1], "%d%% (%s)", (int)(pct > 100 ? 100 : pct), b);
        } else {
            snprintf(s_cells[i][1], sizeof s_cells[i][1], "%s", a[0] && total < 0 ? "(N/A)" : b);
        }
        set_text(entry_at(user0 + 1 + i), s_cells[i][0]);
        set_text(entry_at(value0 + 1 + i), s_cells[i][1]);
        const int32_t mine = who[0] && !strcmp(a, who);
        set_back(entry_at(user0 + 1 + i), mine ? kRed : kGray);
        set_back(entry_at(value0 + 1 + i), mine ? kRed : kGray);
    }
    for (int32_t i = 0; i < s_tableRows; i++) {
        char a[32], b[16];
        table_row(i, a, sizeof a, b, sizeof b);
        sum += atoi(b);
    }

    /* the arrows right of the numbers, the page under them, the totals under the names */
    MenuEntry *v1 = entry_at(value0 + 1), *v15 = entry_at(value0 + kRows), *u15 = entry_at(user0 + kRows);
    MenuEntry *eu = entry_at(up), *ed = entry_at(down), *ep = entry_at(page), *ei = entry_at(info);
    eu->x = (uint16_t)(v1->x + v1->dwWidth + 2);
    eu->y = v1->y;
    ed->x = (uint16_t)(v15->x + v15->dwWidth + 2);
    ed->y = (uint16_t)(v15->y - 6);
    eu->afterDrawCb = ed->afterDrawCb = table_arrow_AfterDraw;
    snprintf(s_tablePageInfo, sizeof s_tablePageInfo, "(%d / %d)", (int)s_tablePage + 1, (int)pages);
    ep->x = (uint16_t)(v15->x + v15->dwWidth - 6);
    ep->y = (uint16_t)(v15->y + v15->dwHeight + 2);
    set_text(ep, s_tablePageInfo);
    ei->x = u15->x;
    ei->y = (uint16_t)(u15->y + u15->dwHeight + 2);
    if (s_tableState == 1)
        snprintf(s_dataInfo, sizeof s_dataInfo, "LOADING...");
    else if (s_tableState < 0)
        snprintf(s_dataInfo, sizeof s_dataInfo, "ONLINE STATS NEED A LOG-IN");
    else
        snprintf(s_dataInfo, sizeof s_dataInfo, "%d PLAYER%s, %d WIN%s", (int)s_tableRows, s_tableRows == 1 ? "" : "S",
                 (int)sum, sum == 1 ? "" : "S");
    set_text(ei, s_dataInfo);
}

static void online_args(char *args, uint32_t cap, int32_t withName)
{
    char v[64], n1[96], n2[96];
    info_get(s_info, "category", v, sizeof v);
    ModApi_JsonEscape(v, n2, sizeof n2);
    if (withName) {
        ModApi_JsonEscape(info_get(s_info, "name", v, sizeof v), n1, sizeof n1);
        snprintf(args, cap, "{\"name\":\"%s\",\"category\":\"%s\"}", n1, n2);
    } else {
        ModApi_JsonEscape(s_cat, n2, sizeof n2);
        snprintf(args, cap, "{\"category\":\"%s\"}", n2);
    }
}

static void scenarioOnlineStatsMenu_OnInit(void) { test_state(); s_tablePage = 0; s_tableRows = 0; s_tableState = 1; }
static void scenarioOnlineStatsMenu_OnDraw(void)
{
    char args[256];
    const int32_t shift = (int32_t)entry_at(ScenarioOnlineStatsMenu_exit)->x - 107;
    if (s_tableState == 1) {
        online_args(args, sizeof args, 1);
        table_fetch("challenges.winners", args);
    }
    if (!s_actHeader[0]) {      /* (opened without the challenge's page: its title or category) */
        char v[64], cat[40];
        info_get(s_info, "title", v, sizeof v);
        if (!v[0])
            snprintf(v, sizeof v, "%s", long_category(info_get(s_info, "category", cat, sizeof cat)));
        snprintf(s_actHeader, sizeof s_actHeader, "%s", v);
    }
    snprintf(s_tableHeader, sizeof s_tableHeader, "%s", s_actHeader);
    set_text(entry_at(ScenarioOnlineStatsMenu_header), s_tableHeader);
    centre(entry_at(ScenarioOnlineStatsMenu_header), s_tableHeader, 24, shift);
    table_draw(ScenarioOnlineStatsMenu_username_000, ScenarioOnlineStatsMenu_win_000, ScenarioOnlineStatsMenu_scrollUp,
               ScenarioOnlineStatsMenu_scrollDown, ScenarioOnlineStatsMenu_pageInfo, ScenarioOnlineStatsMenu_dataInfo,
               ScenarioOnlineStatsMenu_username, "WIN", 0);
}
static void scenarioOnlineStatsMenu_OnReturn(void) {}
static void scenarioOnlineStatsMenu_exit_OnSelect(void) { SetExitMenuFlag(); }
static void scenarioOnlineStatsMenu_scrollUp_onSelect(void) { if (s_tablePage > 0) s_tablePage--; }
static void scenarioOnlineStatsMenu_scrollDown_onSelect(void) { s_tablePage++; }

static void scenarioCompletionMenu_OnInit(void) { test_state(); s_tablePage = 0; s_tableRows = 0; s_tableState = 1; }
static void scenarioCompletionMenu_OnDraw(void)
{
    char args[256];
    const int32_t shift = (int32_t)entry_at(ScenarioCompletionMenu_exit)->x - 107;
    if (s_tableState == 1) {
        online_args(args, sizeof args, 0);
        table_fetch("challenges.ranking", args);
    }
    snprintf(s_tableHeader, sizeof s_tableHeader, "%s (%d)", long_category(s_cat), (int)s_numItems);
    set_text(entry_at(ScenarioCompletionMenu_header), s_tableHeader);
    centre(entry_at(ScenarioCompletionMenu_header), long_category(s_cat), 36, shift);
    table_draw(ScenarioCompletionMenu_username_000, ScenarioCompletionMenu_compcount_000, ScenarioCompletionMenu_scrollUp,
               ScenarioCompletionMenu_scrollDown, ScenarioCompletionMenu_pageInfo, ScenarioCompletionMenu_dataInfo,
               ScenarioCompletionMenu_username, "PROGRESS", s_numItems ? s_numItems : -1);
}
static void scenarioCompletionMenu_OnReturn(void) {}
static void scenarioCompletionMenu_exit_OnSelect(void) { SetExitMenuFlag(); }
static void scenarioCompletionMenu_scrollUp_onSelect(void) { if (s_tablePage > 0) s_tablePage--; }
static void scenarioCompletionMenu_scrollDown_onSelect(void) { s_tablePage++; }

/* ---- CUSTOM CHALLENGES (SWOS 2020's customChallengesMain, SelectSCN, SortBy) ----
 * PLAY: the custom challenges SWOS 2020's players shared on sensiblesoccer.de (.SCN files), from
 * the online-records mod (custom.list / info / download); one picked is downloaded, made one of
 * the player's fetched challenges (src/united_scn.c) and opened on its page. CREATE: the
 * challenges mod's own editor for now. SHARE: not yet -- SWOS 2020 uploads to its server, with no
 * log-in. */

static void customChallengesMainMenu_OnInit(void) { SetCurrentEntry(CustomChallengesMainMenu_play); }
static void customChallengesMainMenu_OnDraw(void)
{
    const int32_t sel = selected_ordinal();
    set_text(entry_at(CustomChallengesMainMenu_desc),
             sel == CustomChallengesMainMenu_create ? "CREATE CUSTOM SCENARIOS"
             : sel == CustomChallengesMainMenu_share ? "SHARE YOUR OWN CREATED SCENARIOS ONLINE"
             : sel == CustomChallengesMainMenu_play ? "PLAY ONLINE SCENARIOS" : "EXIT CUSTOM CHALLENGES");
}
static void customChallengesMainMenu_onBackground(void) {}
static void customChallengesMainMenu_create_onSelect(void)
{
    char out[16];
    call("new", "{}", out, sizeof out);
}
static void customChallengesMainMenu_share_onSelect(void)
{
    ModApi_ShowError("SHARING ONLINE IS NOT IN THIS VERSION YET");
}
static void customChallengesMainMenu_exit_onSelect(void) { SetExitMenuFlag(); }

enum { kScnRows = 15, kScnMax = 600 };
typedef struct { char file[96], team1[40], score[12], team2[40], creator[32], name[96]; int32_t level, done; } ScnRow;
static ScnRow *s_scn;
static int32_t s_numScn, s_scnState, s_infoState, s_scnPage, s_scnSort;   /* sort: 0 date .. 4 level */
static char  s_scnList[1 << 16], s_scnInfo[1 << 18];
static char  s_scnHeader[48], s_scnPageText[24], s_scnData[3][80], s_scnStatus[64];

/* "TEAM1 3-0 TEAM2 BY CREATOR.SCN" (SWOS 2020's names: '/' kept as '_') */
static void scn_parse(const char *file, ScnRow *r)
{
    char stem[96];
    memset(r, 0, sizeof *r);
    snprintf(r->file, sizeof r->file, "%s", file);
    snprintf(stem, sizeof stem, "%s", file);
    char *dot = strstr(stem, ".SCN");
    if (dot)
        *dot = 0;
    for (char *c = stem; *c; c++)
        if (*c == '_')
            *c = '/';
    snprintf(r->name, sizeof r->name, "%s", stem);
    char *by = NULL;
    for (char *p = strstr(stem, " BY "); p; p = strstr(p + 1, " BY "))
        by = p;
    if (by) {
        snprintf(r->creator, sizeof r->creator, "%s", by + 4);
        *by = 0;
    }
    /* the score: the last " a-b " */
    char *score = NULL;
    for (char *p = stem; *p; p++)
        if (p[0] == ' ' && p[1] >= '0' && p[1] <= '9') {
            char *q = p + 1;
            while (*q >= '0' && *q <= '9')
                q++;
            if (*q == '-' && q[1] >= '0' && q[1] <= '9') {
                char *z = q + 1;
                while (*z >= '0' && *z <= '9')
                    z++;
                if (*z == ' ' || !*z)
                    score = p;
            }
        }
    if (score) {
        char *end = strchr(score + 1, ' ');
        snprintf(r->team1, sizeof r->team1, "%.*s", (int)(score - stem), stem);
        snprintf(r->score, sizeof r->score, "%.*s", (int)(end ? end - score - 1 : (long)strlen(score + 1)), score + 1);
        snprintf(r->team2, sizeof r->team2, "%s", end ? end + 1 : "");
    } else {
        snprintf(r->team1, sizeof r->team1, "%s", stem);
    }
}

/* an info line for the row's name: its fields */
static int32_t scn_info(const char *name, char f[7][96])
{
    for (const char *line = s_scnInfo; line && *line;) {
        const char *next = strchr(line, '\n');
        const char *tab = strchr(line, '\t');
        if (tab && (!next || tab < next) && (size_t)(tab - line) == strlen(name) && !strncmp(line, name, strlen(name))) {
            const char *p = line;
            for (int32_t k = 0; k < 7; k++) {
                const char *e = p;
                while (*e && *e != '\t' && *e != '\n')
                    e++;
                snprintf(f[k], 96, "%.*s", (int)(e - p), p);
                p = *e == '\t' ? e + 1 : e;
            }
            return 1;
        }
        line = next ? next + 1 : NULL;
    }
    return 0;
}

static int by_team(const void *a, const void *b) { return strcmp(((const ScnRow *)a)->team1, ((const ScnRow *)b)->team1); }
static int by_creator_team(const void *a, const void *b)
{
    const ScnRow *x = (const ScnRow *)a, *y = (const ScnRow *)b;
    const int c = strcmp(x->creator, y->creator);
    return c ? c : strcmp(x->team1, y->team1);
}
static int by_creator_level(const void *a, const void *b)
{
    const ScnRow *x = (const ScnRow *)a, *y = (const ScnRow *)b;
    const int c = strcmp(x->creator, y->creator);
    return c ? c : x->level - y->level;
}
static int by_level(const void *a, const void *b)
{
    const ScnRow *x = (const ScnRow *)a, *y = (const ScnRow *)b;
    return x->level != y->level ? x->level - y->level : strcmp(x->team1, y->team1);
}

/* the rows, from the list (and the levels from the info, the ticks from the player's online wins) */
static void scn_build(void)
{
    static char mine[1 << 15];
    const int32_t haveMine = ModApi_CallService("online-records", "challenges.completed", "{}", mine, sizeof mine) == 0;
    if (!s_scn && (s_scn = (ScnRow *)malloc(sizeof(ScnRow) * kScnMax)) == NULL)
        return;
    s_numScn = 0;
    for (const char *line = s_scnList; line && *line && s_numScn < kScnMax;) {
        const char *next = strchr(line, '\n');
        char file[96];
        snprintf(file, sizeof file, "%.*s", (int)(next ? next - line : (long)strlen(line)), line);
        if (strstr(file, ".SCN")) {
            ScnRow *r = &s_scn[s_numScn++];
            char f[7][96];
            scn_parse(file, r);
            r->level = scn_info(r->name, f) ? atoi(f[6]) : 0;
            if (haveMine) {
                char key[128];
                snprintf(key, sizeof key, "CUSTOM\t%s\n", r->name);
                r->done = strstr(mine, key) != NULL;
            }
        }
        line = next ? next + 1 : NULL;
    }
    if (s_scnSort == 1)
        qsort(s_scn, (size_t)s_numScn, sizeof *s_scn, by_team);
    else if (s_scnSort == 2)
        qsort(s_scn, (size_t)s_numScn, sizeof *s_scn, by_creator_team);
    else if (s_scnSort == 3)
        qsort(s_scn, (size_t)s_numScn, sizeof *s_scn, by_creator_level);
    else if (s_scnSort == 4)
        qsort(s_scn, (size_t)s_numScn, sizeof *s_scn, by_level);
}

static void scn_level_AfterDraw(MenuEntry *e)
{
    const int32_t i = (int32_t)e->ordinal - CustomChallengesSelectSCNMenu_level_01, idx = s_scnPage * kScnRows + i;
    if (i >= 0 && i < kScnRows && idx < s_numScn && s_scn[idx].level > 0)
        draw_stars(s_scn[idx].level, e->x, e->y - 1);
}

static void scn_team_AfterDraw(MenuEntry *e)
{
    const int32_t i = (int32_t)e->ordinal - CustomChallengesSelectSCNMenu_team1_01, idx = s_scnPage * kScnRows + i;
    if (i >= 0 && i < kScnRows && idx < s_numScn && s_scn[idx].done)
        draw_map(k_check, e->x - 8 + 1, e->y - 3, 0, 0, 12, 13);
}

static void scn_arrow_AfterDraw(MenuEntry *e)
{
    United_DrawPageArrow(e->ordinal == CustomChallengesSelectSCNMenu_scrollDown, e->x + 2, e->y + 1);
}

static void customChallengesSelectSCNMenu_OnInit(void)
{
    s_scnState = s_infoState = 1;
    s_scnPage = 0;
    s_scnStatus[0] = 0;
}
static void customChallengesSelectSCNMenu_OnReturn(void) {}

static void customChallengesSelectSCNMenu_OnDraw(void)
{
    static char cells[kScnRows][4][48];
    const int32_t shift = (int32_t)entry_at(CustomChallengesSelectSCNMenu_exit)->x - 226;
    if (s_scnState == 1 || s_infoState == 1) {
        if (s_infoState == 1)
            s_infoState = ModApi_CallService("online-records", "custom.info", "{}", s_scnInfo, sizeof s_scnInfo);
        if (s_scnState == 1)
            s_scnState = ModApi_CallService("online-records", "custom.list", "{}", s_scnList, sizeof s_scnList);
        if (s_scnState == 0 && s_infoState != 1)
            scn_build();
    }
    snprintf(s_scnHeader, sizeof s_scnHeader, "PLAY ONLINE SCENARIOS (%d)", (int)(s_scnState == 0 ? s_numScn : 0));
    set_text(entry_at(CustomChallengesSelectSCNMenu_header), s_scnHeader);
    centre(entry_at(CustomChallengesSelectSCNMenu_header), "PLAY ONLINE SCENARIOS (000)", 40, shift);

    static const char *const heads[4] = { "HOME TEAM", "", "AWAY TEAM", "CREATOR" };
    static const int32_t col0[4] = { CustomChallengesSelectSCNMenu_team1_00, CustomChallengesSelectSCNMenu_teamScore_00,
                                     CustomChallengesSelectSCNMenu_team2_00, CustomChallengesSelectSCNMenu_share_00 };
    for (int32_t c = 0; c < 4; c++) {
        set_text(entry_at(col0[c]), heads[c]);
        set_back(entry_at(col0[c]), kLightBrownWithYellowFrame);
    }
    entry_at(CustomChallengesSelectSCNMenu_level_00)->isInvisible = 1;

    const int32_t sel = selected_ordinal();
    int32_t selRow = -1;
    for (int32_t c = 0; c < 4; c++)
        if (sel >= col0[c] + 1 && sel <= col0[c] + kScnRows)
            selRow = sel - col0[c] - 1;
    for (int32_t i = 0; i < kScnRows; i++) {
        const int32_t idx = s_scnPage * kScnRows + i;
        const ScnRow *r = s_scnState == 0 && idx < s_numScn ? &s_scn[idx] : NULL;
        snprintf(cells[i][0], 48, "%s", r ? r->team1 : "");
        snprintf(cells[i][1], 48, "%s", r ? r->score : "");
        snprintf(cells[i][2], 48, "%s", r ? r->team2 : "");
        snprintf(cells[i][3], 48, "%s", r ? r->creator : "");
        for (int32_t c = 0; c < 4; c++) {
            MenuEntry *e = entry_at(col0[c] + 1 + i);
            shorten(cells[i][c], e, (int32_t)e->dwWidth - 4);
            set_text(e, cells[i][c]);
            set_back(e, i == selRow ? kRed : kGray);
        }
        entry_at(CustomChallengesSelectSCNMenu_level_01 + i)->afterDrawCb = scn_level_AfterDraw;
        entry_at(CustomChallengesSelectSCNMenu_team1_01 + i)->afterDrawCb = scn_team_AfterDraw;
    }

    /* the page and its arrows right of the table, the picked one's texts under it */
    const int32_t pages = s_numScn && s_scnState == 0 ? (s_numScn + kScnRows - 1) / kScnRows : 1;
    if (s_scnPage >= pages)
        s_scnPage = pages - 1;
    {
        MenuEntry *l1 = entry_at(CustomChallengesSelectSCNMenu_level_01), *l15 = entry_at(CustomChallengesSelectSCNMenu_level_15);
        MenuEntry *u = entry_at(CustomChallengesSelectSCNMenu_scrollUp), *d = entry_at(CustomChallengesSelectSCNMenu_scrollDown);
        MenuEntry *pg = entry_at(CustomChallengesSelectSCNMenu_pageInfo);
        u->x = d->x = (uint16_t)(l15->x + l15->dwWidth + 1);
        u->y = l1->y;
        d->y = (uint16_t)(l15->y - 6);
        u->afterDrawCb = d->afterDrawCb = scn_arrow_AfterDraw;
        snprintf(s_scnPageText, sizeof s_scnPageText, "(%d / %d)", (int)s_scnPage + 1, (int)pages);
        pg->x = (uint16_t)(l15->x - 10);
        pg->y = (uint16_t)(l15->y + l15->dwHeight + 2);
        set_text(pg, s_scnPageText);
        for (int32_t k = 0; k < 3; k++) {
            MenuEntry *e = entry_at(CustomChallengesSelectSCNMenu_dataInfo1 + k);
            e->y = (uint16_t)(l15->y + l15->dwHeight + 12 + 9 * k);
            e->content = ENTRY_STRING;
            set_text(e, s_scnData[k]);
        }
    }
    {
        char f[7][96];
        const int32_t idx = s_scnPage * kScnRows + selRow;
        s_scnData[0][0] = s_scnData[1][0] = s_scnData[2][0] = 0;
        if (s_scnStatus[0])
            snprintf(s_scnData[1], sizeof s_scnData[1], "%s", s_scnStatus);
        else if (s_scnState == 1)
            snprintf(s_scnData[1], sizeof s_scnData[1], "CHECKING ONLINE SCENARIO INFORMATION...");
        else if (s_scnState < 0)
            snprintf(s_scnData[1], sizeof s_scnData[1], "SENSIBLESOCCER.DE CANNOT BE REACHED");
        else if (selRow >= 0 && idx < s_numScn && scn_info(s_scn[idx].name, f)) {
            snprintf(s_scnData[0], sizeof s_scnData[0], "%s", f[1]);
            snprintf(s_scnData[1], sizeof s_scnData[1], "%s, %s", f[2], f[3]);
            snprintf(s_scnData[2], sizeof s_scnData[2], "%s", f[5]);
        }
        set_color(entry_at(CustomChallengesSelectSCNMenu_dataInfo1), kYellowText);
    }
    {
        static const char *const sorts[5] = { "SORTED BY DATE", "SORTED BY TEAM", "SORTED BY CREATOR / TEAM",
                                              "SORTED BY CREATOR / LEVEL", "SORTED BY LEVEL" };
        set_text(entry_at(CustomChallengesSelectSCNMenu_sortedBy), sorts[s_scnSort]);
    }
}

static void customChallengesSelectSCNMenu_select_onSelect(void)
{
    const int32_t sel = selected_ordinal();
    int32_t row = -1;
    static const int32_t col0[4] = { CustomChallengesSelectSCNMenu_team1_00, CustomChallengesSelectSCNMenu_teamScore_00,
                                     CustomChallengesSelectSCNMenu_team2_00, CustomChallengesSelectSCNMenu_share_00 };
    for (int32_t c = 0; c < 4; c++)
        if (sel >= col0[c] + 1 && sel <= col0[c] + kScnRows)
            row = sel - col0[c] - 1;
    const int32_t idx = s_scnPage * kScnRows + row;
    if (row < 0 || s_scnState != 0 || idx >= s_numScn)
        return;
    /* the download: SWOS 2020 waited for it too (its hourglass) -- a second or two */
    char args[160], e[140], out[32];
    snprintf(args, sizeof args, "{\"file\":\"%s\"}", ModApi_JsonEscape(s_scn[idx].file, e, sizeof e));
    int32_t rc = 1;
    for (int32_t waited = 0; rc == 1 && waited < 15000; waited += 20) {
        rc = ModApi_CallService("online-records", "custom.download", args, out, sizeof out);
        if (rc == 1)
            SDL_Delay(20);
    }
    if (rc != 0) {
        snprintf(s_scnStatus, sizeof s_scnStatus, "THE SCENARIO COULD NOT BE DOWNLOADED");
        return;
    }
    uint32_t size = 0;
    unsigned char *data = (unsigned char *)ModApi_LoadFile("challenges", "download.scn", &size);
    char id[48] = "";
    const int32_t ok = data && UnitedScn_Import(data, size, id, sizeof id);
    free(data);
    if (!ok) {
        snprintf(s_scnStatus, sizeof s_scnStatus, "THE SCENARIO COULD NOT BE READ");
        return;
    }
    s_scnStatus[0] = 0;
    call("reload", "{}", out, sizeof out);
    snprintf(s_sel, sizeof s_sel, "%s", id);
    snprintf(s_cat, sizeof s_cat, "CUSTOM");
    s_actHeader[0] = 0;
    show("swos2020/selectScenarioActionMenu");
}

static void customChallengesSelectSCNMenu_scrollUp_onSelect(void) { if (s_scnPage > 0) s_scnPage--; }
static void customChallengesSelectSCNMenu_scrollDown_onSelect(void) { s_scnPage++; }
static void customChallengesSelectSCNMenu_completion_OnSelect(void)
{
    snprintf(s_cat, sizeof s_cat, "CUSTOM");
    s_numItems = s_numScn;
    show("swos2020/scenarioCompletionMenu");
}
static void customChallengesSelectSCNMenu_sortedBy_OnSelect(void)
{
    show("swos2020/customChallengesSortByMenu");
    if (s_scnState == 0)
        scn_build();
    s_scnPage = 0;
}
static void customChallengesSelectSCNMenu_exit_OnSelect(void) { SetExitMenuFlag(); }
static void customChallengesMainMenu_play_onSelect(void) { show("swos2020/customChallengesSelectSCNMenu"); }

static void customChallengesSortByMenu_onDraw(void) {}
static void sort_by(int32_t k) { s_scnSort = k; SetExitMenuFlag(); }
static void customChallengesSortByMenu_sortByDate_onSelect(void) { sort_by(0); }
static void customChallengesSortByMenu_sortByTeam_onSelect(void) { sort_by(1); }
static void customChallengesSortByMenu_sortByCreatorAndTeam_onSelect(void) { sort_by(2); }
static void customChallengesSortByMenu_sortByCreatorAndLevel_onSelect(void) { sort_by(3); }
static void customChallengesSortByMenu_sortByLevel_onSelect(void) { sort_by(4); }
static void customChallengesSortByMenu_exit_onSelect(void) { SetExitMenuFlag(); }

/* ---- the mod ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(selectCategoryMenu_OnInit), HANDLER(selectCategoryMenu_OnDraw), HANDLER(selectCategoryMenu_OnReturn),
    HANDLER(selectCategoryMenu_OnBackground), HANDLER(selectCategoryMenu_exit_onSelect),
    HANDLER(cat2_onSelect), HANDLER(cat3_onSelect), HANDLER(cat4_onSelect), HANDLER(cat5_onSelect),
    HANDLER(cat6_onSelect), HANDLER(cat7_onSelect), HANDLER(cat8_onSelect), HANDLER(catX_onSelect),
    HANDLER(scenarioModeMainMenu_OnInit), HANDLER(scenarioModeMainMenu_OnDraw), HANDLER(scenarioModeMainMenu_OnReturn),
    HANDLER(scenarioModeMainMenu_OnBackground), HANDLER(scenarioModeMainMenu_OnExit),
    HANDLER(scenarioModeMain_scrollUp_onSelect), HANDLER(scenarioModeMain_scrollDown_onSelect),
    HANDLER(s00_OnSelect), HANDLER(s01_OnSelect), HANDLER(s02_OnSelect), HANDLER(s10_OnSelect),
    HANDLER(s11_OnSelect), HANDLER(s12_OnSelect), HANDLER(s20_OnSelect), HANDLER(s21_OnSelect),
    HANDLER(s22_OnSelect), HANDLER(s30_OnSelect), HANDLER(s31_OnSelect), HANDLER(s32_OnSelect),
    HANDLER(scenCompletion_OnSelect),
    HANDLER(selectScenarioActionMenu_OnInit), HANDLER(selectScenarioActionMenu_OnDraw),
    HANDLER(selectScenarioActionMenu_onSelect), HANDLER(act1_onSelect), HANDLER(act2_onSelect),
    HANDLER(scenarioOnlineStatsMenu_OnInit), HANDLER(scenarioOnlineStatsMenu_OnDraw),
    HANDLER(scenarioOnlineStatsMenu_OnReturn), HANDLER(scenarioOnlineStatsMenu_exit_OnSelect),
    HANDLER(scenarioOnlineStatsMenu_scrollUp_onSelect), HANDLER(scenarioOnlineStatsMenu_scrollDown_onSelect),
    HANDLER(scenarioCompletionMenu_OnInit), HANDLER(scenarioCompletionMenu_OnDraw),
    HANDLER(scenarioCompletionMenu_OnReturn), HANDLER(scenarioCompletionMenu_exit_OnSelect),
    HANDLER(scenarioCompletionMenu_scrollUp_onSelect), HANDLER(scenarioCompletionMenu_scrollDown_onSelect),
    HANDLER(customChallengesMainMenu_OnInit), HANDLER(customChallengesMainMenu_OnDraw),
    HANDLER(customChallengesMainMenu_onBackground), HANDLER(customChallengesMainMenu_create_onSelect),
    HANDLER(customChallengesMainMenu_share_onSelect), HANDLER(customChallengesMainMenu_play_onSelect),
    HANDLER(customChallengesMainMenu_exit_onSelect),
    HANDLER(customChallengesSelectSCNMenu_OnInit), HANDLER(customChallengesSelectSCNMenu_OnDraw),
    HANDLER(customChallengesSelectSCNMenu_OnReturn), HANDLER(customChallengesSelectSCNMenu_select_onSelect),
    HANDLER(customChallengesSelectSCNMenu_scrollUp_onSelect), HANDLER(customChallengesSelectSCNMenu_scrollDown_onSelect),
    HANDLER(customChallengesSelectSCNMenu_completion_OnSelect), HANDLER(customChallengesSelectSCNMenu_sortedBy_OnSelect),
    HANDLER(customChallengesSelectSCNMenu_exit_OnSelect),
    HANDLER(customChallengesSortByMenu_onDraw), HANDLER(customChallengesSortByMenu_sortByDate_onSelect),
    HANDLER(customChallengesSortByMenu_sortByTeam_onSelect), HANDLER(customChallengesSortByMenu_sortByCreatorAndTeam_onSelect),
    HANDLER(customChallengesSortByMenu_sortByCreatorAndLevel_onSelect), HANDLER(customChallengesSortByMenu_sortByLevel_onSelect),
    HANDLER(customChallengesSortByMenu_exit_onSelect),
};
#undef HANDLER

/* "challenges-screens": the challenges mod opens these in place of its own */
static int32_t screens(const char *method, const char *args, char *out, uint32_t cap)
{
    (void)args; (void)out; (void)cap;
    if (strcmp(method, "open") != 0)
        return -1;
    s_catPage = 0;
    fetch_categories();
    if (!s_numCats)
        return -1;                          /* (nothing to show: the mod's own screen says so) */
    show("swos2020/selectCategoryMenu");
    return 0;
}

/* SWOS_SCN_IMPORT_TEST=<file.SCN>: at the first menu frame, the file imported (src/united_scn.c),
 * the challenges read again and the challenge's fields printed (SCN TEST ...), then exit. */
static void scn_import_test(void)
{
    static int32_t done;
    const char *path = getenv("SWOS_SCN_IMPORT_TEST");
    if (done || !path)
        return;
    done = 1;
    FILE *f = strncmp(path, "online:", 7) ? fopen(path, "rb") : NULL;
    unsigned char *data = NULL;
    long n = 0;
    if (!strncmp(path, "online:", 7)) {     /* (a shared one, downloaded as PLAY does) */
        char args[200], e[160], out[32];
        int32_t rc = 1;
        snprintf(args, sizeof args, "{\"file\":\"%s\"}", ModApi_JsonEscape(path + 7, e, sizeof e));
        for (int32_t waited = 0; rc == 1 && waited < 20000; waited += 20) {
            rc = ModApi_CallService("online-records", "custom.download", args, out, sizeof out);
            if (rc == 1)
                SDL_Delay(20);
        }
        uint32_t size = 0;
        data = rc == 0 ? (unsigned char *)ModApi_LoadFile("challenges", "download.scn", &size) : NULL;
        n = (long)size;
    }
    if (f) {
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = n > 0 ? (unsigned char *)malloc((size_t)n) : NULL;
        if (data && fread(data, 1, (size_t)n, f) != (size_t)n)
            n = 0;
        fclose(f);
    }
    char id[48] = "", args[96], out[4096] = "";
    const int32_t ok = data && n > 0 && UnitedScn_Import(data, (uint32_t)n, id, sizeof id);
    free(data);
    call("reload", "{}", out, sizeof out);
    snprintf(args, sizeof args, "{\"id\":\"%s\"}", id);
    const int32_t got = ok && call("info", args, out, sizeof out) == 0;
    printf("SCN TEST %s: %s %s\n%s", path, ok ? "imported" : "NOT imported", id, got ? out : "(no info)\n");
    printf("SCN TEST %s\n", ok && got ? "PASS" : "FAIL");
    fflush(stdout);
    exit(ok && got ? 0 : 1);
}

static void challenges2020_init(void)
{
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    ModApi_RegisterService(SLUG, "challenges-screens", screens);
}

const ModDef ModDef_challenges2020 = {
    .slug = SLUG,
    .init = challenges2020_init,
    .menuDrawn = scn_import_test,
};

#else
const ModDef ModDef_challenges2020 = { .slug = "challenges2020" };
#endif
